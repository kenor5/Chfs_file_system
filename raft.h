#ifndef raft_h
#define raft_h

#include <atomic>
#include <mutex>
#include <chrono>
#include <thread>
#include <ctime>
#include <algorithm>
#include <thread>
#include <stdarg.h>
#include <numeric>

#include "rpc.h"
#include "raft_storage.h"
#include "raft_protocol.h"
#include "raft_state_machine.h"

template <typename state_machine, typename command>
class raft {
    static_assert(std::is_base_of<raft_state_machine, state_machine>(), "state_machine must inherit from raft_state_machine");
    static_assert(std::is_base_of<raft_command, command>(), "command must inherit from raft_command");

    friend class thread_pool;

// #define RAFT_LOG(fmt, args...) \
//     do {                       \
//     } while (0);

    #define RAFT_LOG(fmt, args...)                                                                                   \
    do {                                                                                                         \
        auto now =                                                                                               \
            std::chrono::duration_cast<std::chrono::milliseconds>(                                               \
                std::chrono::system_clock::now().time_since_epoch())                                             \
                .count();                                                                                        \
        printf("[%lld][%s:%d][node %d term %d] " fmt "\n", now, __FILE__, __LINE__, my_id, current_term, ##args); \
    } while (0);

public:
    raft(
        rpcs *rpc_server,
        std::vector<rpcc *> rpc_clients,
        int idx,
        raft_storage<command> *storage,
        state_machine *state);
    ~raft();

    // start the raft node.
    // Please make sure all of the rpc request handlers have been registered before this method.
    void start();

    // stop the raft node.
    // Please make sure all of the background threads are joined in this method.
    // Notice: you should check whether is server should be stopped by calling is_stopped().
    //         Once it returns true, you should break all of your long-running loops in the background threads.
    void stop();

    // send a new command to the raft nodes.
    // This method returns true if this raft node is the leader that successfully appends the log.
    // If this node is not the leader, returns false.
    bool new_command(command cmd, int &term, int &index);

    // returns whether this node is the leader, you should also set the current term;
    bool is_leader(int &term);

    // save a snapshot of the state machine and compact the log.
    bool save_snapshot();

private:
    std::mutex mtx; // A big lock to protect the whole data structure
    ThrPool *thread_pool;
    raft_storage<command> *storage; // To persist the raft log
    state_machine *state;           // The state machine that applies the raft log, e.g. a kv store

    rpcs *rpc_server;                // RPC server to recieve and handle the RPC requests
    std::vector<rpcc *> rpc_clients; // RPC clients of all raft nodes including this node
    int my_id;                       // The index of this node in rpc_clients, start from 0

    std::atomic_bool stopped;

    enum raft_role {
        follower,
        candidate,
        leader
    };
    raft_role role;
    int current_term;
    int leader_id;

    std::thread *background_election;
    std::thread *background_ping;
    std::thread *background_commit;
    std::thread *background_apply;

    // Your code here:

    const int timeout_vote = 300;
    const int timeout_election = 500;

    /* ----Persistent state on all server----  */
    int votedFor;
    std::vector<log_entry<command>> log;

    /* ---- Volatile state on all server----  */
    int commitIndex;
    int lastApplied;
    long long last_rpc_time;
    std::vector<bool> votes;

    /* ---- Volatile state on leader----  */
    std::vector<int> nextIndex, matchIndex;

private:
    // RPC handlers
    int request_vote(request_vote_args arg, request_vote_reply &reply);

    int append_entries(append_entries_args<command> arg, append_entries_reply &reply);

    int install_snapshot(install_snapshot_args arg, install_snapshot_reply &reply);

    // RPC helpers
    void send_request_vote(int target, request_vote_args arg);
    void handle_request_vote_reply(int target, const request_vote_args &arg, const request_vote_reply &reply);

    void send_append_entries(int target, append_entries_args<command> arg);
    void handle_append_entries_reply(int target, const append_entries_args<command> &arg, const append_entries_reply &reply);

    void send_install_snapshot(int target, install_snapshot_args arg);
    void handle_install_snapshot_reply(int target, const install_snapshot_args &arg, const install_snapshot_reply &reply);

private:
    bool is_stopped();
    int num_nodes() {
        return rpc_clients.size();
    }

    // background workers
    void run_background_ping();
    void run_background_election();
    void run_background_commit();
    void run_background_apply();

    // Your code here:
    long long get_current_time();
    int get_random_time(int limit);
    int get_random(int, int);
};

template <typename state_machine, typename command>
raft<state_machine, command>::raft(rpcs *server, std::vector<rpcc *> clients, int idx, raft_storage<command> *storage, state_machine *state) :
    stopped(false),
    rpc_server(server),
    rpc_clients(clients),
    my_id(idx),
    storage(storage),
    state(state),
    background_election(nullptr),
    background_ping(nullptr),
    background_commit(nullptr),
    background_apply(nullptr),
    current_term(0),
    role(follower) {
    thread_pool = new ThrPool(32);

    // Register the rpcs.
    rpc_server->reg(raft_rpc_opcodes::op_request_vote, this, &raft::request_vote);
    rpc_server->reg(raft_rpc_opcodes::op_append_entries, this, &raft::append_entries);
    rpc_server->reg(raft_rpc_opcodes::op_install_snapshot, this, &raft::install_snapshot);

    // Your code here:
    // Do the initialization
    votedFor = -1;
    commitIndex = lastApplied = 0;
    last_rpc_time = get_current_time();
    RAFT_LOG("init time%lld", last_rpc_time);

    log = std::vector<log_entry<command>>();
    log_entry<command> init = log_entry<command>();  // 0th log
    init.term = init.index = 0;
    log.push_back(init);
}

template <typename state_machine, typename command>
raft<state_machine, command>::~raft() {
    if (background_ping) {
        delete background_ping;
    }
    if (background_election) {
        delete background_election;
    }
    if (background_commit) {
        delete background_commit;
    }
    if (background_apply) {
        delete background_apply;
    }
    delete thread_pool;
}

/******************************************************************

                        Public Interfaces

*******************************************************************/

template <typename state_machine, typename command>
void raft<state_machine, command>::stop() {
    stopped.store(true);
    background_ping->join();
    background_election->join();
    background_commit->join();
    background_apply->join();
    thread_pool->destroy();
}

template <typename state_machine, typename command>
bool raft<state_machine, command>::is_stopped() {
    return stopped.load();
}

template <typename state_machine, typename command>
bool raft<state_machine, command>::is_leader(int &term) {
    term = current_term;
    return role == leader;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::start() {
    // Lab3: Your code here

    RAFT_LOG("start");
    this->background_election = new std::thread(&raft::run_background_election, this);
    this->background_ping = new std::thread(&raft::run_background_ping, this);
    this->background_commit = new std::thread(&raft::run_background_commit, this);
    this->background_apply = new std::thread(&raft::run_background_apply, this);
}

template <typename state_machine, typename command>
bool raft<state_machine, command>::new_command(command cmd, int &term, int &index) {
    // Lab3: Your code here
    mtx.lock();
    if (is_leader(current_term)) {
        term = current_term;

        int size = log.size();
        auto cur_log = log_entry<command>(cmd, current_term, size);
        log.push_back(cur_log);

        index = size -1;
        mtx.unlock();
        return true;
    }

    mtx.unlock();
    
    return false;
}

template <typename state_machine, typename command>
bool raft<state_machine, command>::save_snapshot() {
    // Lab3: Your code here
    return true;
}

/******************************************************************

                         RPC Related

*******************************************************************/
template <typename state_machine, typename command>
int raft<state_machine, command>::request_vote(request_vote_args args, request_vote_reply &reply) {
    // Lab3: Your code here
    mtx.lock();


    if (current_term > args.term) {
        reply.voteGranted = false;
        reply.term = current_term;
    }else if (args.candidateId == votedFor || -1 == votedFor) {
        if ((log[commitIndex].term < args.lastLogTerm) || (log[commitIndex].term == args.lastLogTerm && commitIndex <= args.lastLogIndex)) {
            reply.voteGranted = true;
            reply.term = current_term;
            votedFor = args.candidateId;
            RAFT_LOG("%d vote for %d", my_id, args.candidateId);
        }
    }else {
        reply.voteGranted = false;
        reply.term = current_term;
    }


    mtx.unlock();
    return 0;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::handle_request_vote_reply(int target, const request_vote_args &arg, const request_vote_reply &reply) {
    // Lab3: Your code here
    mtx.lock();
    last_rpc_time = get_current_time();
    if (reply.term > current_term) {
        RAFT_LOG("%dreveive biger term %d > %d",my_id, arg.term, current_term);
        current_term = reply.term;
        role = follower;
        votedFor = -1;

    }else {
        this->votes[target] = reply.voteGranted;
    }

    mtx.unlock();

    return;
}

template <typename state_machine, typename command>
int raft<state_machine, command>::append_entries(append_entries_args<command> arg, append_entries_reply &reply) {
    // Lab3: Your code here
    mtx.lock();

    last_rpc_time = get_current_time();

    if (arg.heartbeat) {
        if (arg.term >= current_term) {
            // RAFT_LOG("%d receive heart beat from %d, %lld",my_id, arg.leaderId, last_rpc_time);
            role = follower;
            current_term = arg.term;
            reply.term = arg.term;
            reply.success = true;
        }else {
            reply.success = false;
            reply.term = current_term;
        }
    }else if (current_term > arg.term || arg.prevLogIndex > (int)log.size() || arg.prevLogTerm != log[arg.prevLogIndex].term) {
        reply.term = current_term;
        reply.success = false;
    }else {
        int idx = arg.prevLogIndex;
        while(++idx) {
            if (idx > (int)arg.entries.size() || idx > (int)log.size()) break;
            if (arg.entries[idx].term != log[idx].term) break;
        }

        while (idx < (int)log.size()) log.pop_back();

        while (idx < (int)arg.entries.size()) {
            log.push_back(arg.entries[idx]);
            idx++;
        }

        auto min = [](int a, int b)->int{
            if (a>b) return b; return a;
        };

        if (arg.leaderCommit > commitIndex) commitIndex = min(arg.leaderCommit, (int)log.size()-1);

        reply.term = current_term;
        reply.success = true;
    }
    mtx.unlock();
    return 0;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::handle_append_entries_reply(int node, const append_entries_args<command> &arg, const append_entries_reply &reply) {
    // Lab3: Your code here
    mtx.lock();

    last_rpc_time = get_current_time();
    if (reply.term > current_term) {
        RAFT_LOG("%d receive biger term %d",  my_id, reply.term);
        current_term = reply.term;
        role = follower;
        votedFor = -1;
    }
    if (arg.heartbeat) {
        mtx.unlock();
        return;
    }

    if (reply.success) {
        auto max  = [](int a, int b)->int{
            if (a>b) return a; return b;
        };
        matchIndex[node] = max(matchIndex[node], (int)arg.entries.size()-1);

        auto v = matchIndex;
        std::sort(v.begin(), v.end(), std::less<int>());

        commitIndex = max(commitIndex, v[(v.size()+1)/2-1]);
    }else nextIndex[node] = 1;

    //     if (reply.term > current_term) {
    //     /* update current_term and become follower */
    //     current_term = reply.term;
        
    //     role = follower;
    //     votedFor = -1;

    //     /* persist metadata state: term changes */

    // } else if (arg.heartbeat) {}
    // else if (reply.success) {
    //     matchIndex[node] = std::max(matchIndex[node], (int) arg.entries.size() - 1);

    //     std::vector<int> v = matchIndex;
    //     std::sort(v.begin(), v.end(), std::less<int>());
    //     commitIndex = std::max(commitIndex, v[(v.size() + 1) / 2 - 1]);

    //     RAFT_LOG("~~ id: %d, commitIndex: %d", node, commitIndex);
    // } else {
    //     RAFT_LOG("decrement: target: %d", node);
    //     nextIndex[node] = 1;
    // }
    mtx.unlock();
    return;
}

template <typename state_machine, typename command>
int raft<state_machine, command>::install_snapshot(install_snapshot_args args, install_snapshot_reply &reply) {
    // Lab3: Your code here
    return 0;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::handle_install_snapshot_reply(int node, const install_snapshot_args &arg, const install_snapshot_reply &reply) {
    // Lab3: Your code here
    return;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::send_request_vote(int target, request_vote_args arg) {
    request_vote_reply reply;
    if (rpc_clients[target]->call(raft_rpc_opcodes::op_request_vote, arg, reply) == 0) {
        handle_request_vote_reply(target, arg, reply);
    } else {
        // RPC fails
    }
}

template <typename state_machine, typename command>
void raft<state_machine, command>::send_append_entries(int target, append_entries_args<command> arg) {
    append_entries_reply reply;
    if (rpc_clients[target]->call(raft_rpc_opcodes::op_append_entries, arg, reply) == 0) {
        handle_append_entries_reply(target, arg, reply);
    } else {
        // RPC fails
    }
}

template <typename state_machine, typename command>
void raft<state_machine, command>::send_install_snapshot(int target, install_snapshot_args arg) {
    install_snapshot_reply reply;
    if (rpc_clients[target]->call(raft_rpc_opcodes::op_install_snapshot, arg, reply) == 0) {
        handle_install_snapshot_reply(target, arg, reply);
    } else {
        // RPC fails
    }
}

/******************************************************************

                        Background Workers

*******************************************************************/

template <typename state_machine, typename command>
void raft<state_machine, command>::run_background_election() {
    // Periodly check the liveness of the leader.

    // Work for followers and candidates.

    srand(time(NULL));
    while (true) {
        // RAFT_LOG("role:%d,id:%d,term:%d,lasttime:%lld", role, my_id, current_term, last_rpc_time);
        if (is_stopped()) return;
        // Lab3: Your code here
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        long long current_time = get_current_time();
        long long rand_time = (long long)get_random_time(100);
        if (role == follower) {
            if (current_time - last_rpc_time > rand_time + timeout_vote) {
                role = candidate;
                votedFor = my_id;
                // RAFT_LOG("%dterm before%d", my_id, current_term);
                current_term++;
                // RAFT_LOG("%dterm inc %d",my_id,  current_term);
            
                votes.assign(rpc_clients.size(), false);
                votes[my_id] = true;

                request_vote_args arg(current_term, my_id, log.back().term, log.size()-1);

                last_rpc_time = get_current_time();
                for (int i = 0; i < (int)rpc_clients.size(); ++i) {
                    if (i == my_id) continue;
                    thread_pool->addObjJob(this, &raft::send_request_vote, i, arg);
                }
            }
        }else if (role == candidate) {
            int sum = 0;
            int size = rpc_clients.size();
            for (auto i: votes) sum+=i;
            if (sum > size/2) {
                nextIndex.assign(size, log.size());
                matchIndex.assign(size, 0);
                role = leader;
                RAFT_LOG("%d become leader", my_id);
            }
            else if (current_time - last_rpc_time > timeout_election) {
                // RAFT_LOG("%d fails become leader", my_id);
                last_rpc_time = get_current_time();
                role = follower;
                votedFor = -1;
                // RAFT_LOG("term before%d", current_term);
                current_term--;
                // RAFT_LOG("term dec %d", current_term);
            }
        }
    }    
    
    return;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::run_background_commit() {
    // Periodly send logs to the follower.

    // Only work for the leader.

    
    while (true) {
        if (is_stopped()) return;
        // Lab3: Your code here:
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (!is_leader(current_term)) continue;
        matchIndex[my_id] = log.size()-1;
        for (int i = 0; i < (int)log.size(); ++i) {
            if (i == my_id || (int)log.size() > nextIndex[i]) continue;
            append_entries_args<command> arg(false, current_term, my_id, nextIndex[i] - 1, log[nextIndex[i] - 1].term, log, commitIndex);
            thread_pool->addObjJob(this, &raft::send_append_entries, i, arg);
        }
    }    
        

    return;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::run_background_apply() {
    // Periodly apply committed logs the state machine

    // Work for all the nodes.

    
    while (true) {
        if (is_stopped()) return;
        // Lab3: Your code here:
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (commitIndex <= lastApplied) continue;
        for (int i = lastApplied+1; i <= commitIndex; ++i) {
            this->state->apply_log(log[i].cmd);
        } 
        lastApplied = commitIndex;
       
    }    
    
    return;
}

template <typename state_machine, typename command>
void raft<state_machine, command>::run_background_ping() {
    // Periodly send empty append_entries RPC to the followers.

    // Only work for the leader.

    
    while (true) {
        if (is_stopped()) return;
        // Lab3: Your code here:
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        if (!is_leader(current_term)) continue;
        for (int i = 0; i < (int)rpc_clients.size(); ++i) {
            if (i == my_id) continue;
            append_entries_args<command> arg(true, current_term, my_id, 0, 0, std::vector<log_entry<command>>(), matchIndex[i]);
            thread_pool->addObjJob(this, &raft::send_append_entries, i, arg);
        }

    }    

    return;
}

/******************************************************************

                        Other functions

*******************************************************************/


template<typename state_machine, typename command>
long long raft<state_machine, command>::get_current_time () {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

template<typename state_machine, typename command>
int raft<state_machine, command>::get_random_time(int limit) {
    assert(limit > 0);
    return rand()% limit;
}


template<typename state_machine, typename command>
int raft<state_machine, command>::get_random (int lower, int upper) {
    assert(upper >= lower);
    int ans = rand() % (upper - lower) + lower;
    return ans;
}

#endif // raft_h