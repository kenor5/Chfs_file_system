// chfs client.  implements FS operations using extent and lock server
#include "chfs_client.h"
#include "extent_client.h"
#include <sstream>
#include <iostream>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>


chfs_client::chfs_client(std::string extent_dst)
{
    ec = new extent_client(extent_dst);
    if (ec->put(1, "") != extent_protocol::OK)
        printf("error init root dir\n"); // XYB: init root dir
}

chfs_client::inum
chfs_client::n2i(std::string n)
{
    std::istringstream ist(n);
    unsigned long long finum;
    ist >> finum;
    return finum;
}

std::string
chfs_client::filename(inum inum)
{
    std::ostringstream ost;
    ost << inum;
    return ost.str();
}

bool
chfs_client::isfile(inum inum)
{
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_FILE) {
        printf("isfile: %lld is a file\n", inum);
        return true;
    } 
    printf("isfile: %lld is not a file\n", inum);
    return false;
}
/** Your code here for Lab...
 * You may need to add routines such as
 * readlink, issymlink here to implement symbolic link.
 * 
 * */

bool
chfs_client::issymlink(inum inum)
{

    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_SYMLINK) {
        printf("isfile: %lld is a symlink\n", inum);
        return true;
    } 
    printf("isfile: %lld is not a symlink\n", inum);
    return false;
}

bool
chfs_client::isdir(inum inum)
{
    // Oops! is this still correct when you implement symlink?
    extent_protocol::attr a;

    if (ec->getattr(inum, a) != extent_protocol::OK) {
        printf("error getting attr\n");
        return false;
    }

    if (a.type == extent_protocol::T_DIR) {
        printf("isfile: %lld is a dir\n", inum);
        return true;
    } 
    printf("isfile: %lld is not a dir\n", inum);
    return false;
}

int
chfs_client::getfile(inum inum, fileinfo &fin)
{
    int r = OK;

    printf("getfile %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }

    fin.atime = a.atime;
    fin.mtime = a.mtime;
    fin.ctime = a.ctime;
    fin.size = a.size;
    printf("getfile %016llx -> sz %llu\n", inum, fin.size);

release:
    return r;
}

int
chfs_client::getdir(inum inum, dirinfo &din)
{
    int r = OK;

    printf("getdir %016llx\n", inum);
    extent_protocol::attr a;
    if (ec->getattr(inum, a) != extent_protocol::OK) {
        r = IOERR;
        goto release;
    }
    din.atime = a.atime;
    din.mtime = a.mtime;
    din.ctime = a.ctime;

release:
    return r;
}


#define EXT_RPC(xx) do { \
    if ((xx) != extent_protocol::OK) { \
        printf("EXT_RPC Error: %s:%d \n", __FILE__, __LINE__); \
        r = IOERR; \
        goto release; \
    } \
} while (0)

// Only support set size of attr
int
chfs_client::setattr(inum ino, size_t size)
{
    int r = OK;

    /*
     * your code goes here.
     * note: get the content of inode ino, and modify its content
     * according to the size (<, =, or >) content length.
     */
    std::string buf;
    int rt = ec->get(ino, buf);
    if (rt != OK) {
        return rt;
    }

    buf.resize(size);
    ec->put(ino, buf);


    return r;
}

int
chfs_client::create(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if file exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    bool found;
    inum tmp;

    assert(lookup(parent, name, found, tmp) == OK);
    if (found == 1) {
        printf("file already exist!");
        return EXIST;
    }
    ec->create(extent_protocol::T_FILE, ino_out);

    std::string buf;
    assert(ec->get(parent, buf) == OK);

    buf.append(name);
    buf.append(":");
    buf.append(std::to_string(ino_out));
    buf.append("/");

    ec->put(parent, buf);
    return r;
}

int
chfs_client::mkdir(inum parent, const char *name, mode_t mode, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup is what you need to check if directory exist;
     * after create file or dir, you must remember to modify the parent infomation.
     */
    bool found;
    inum tmp;

    assert(lookup(parent, name, found, tmp) == OK);
    if (found == 1) {
        printf("dir already exist!");
        return EXIST;
    }

    ec->create(extent_protocol::T_DIR, ino_out);

    std::string buf;
    assert(ec->get(parent, buf) == OK);

    buf.append(name);
    buf.append(":");
    buf.append(std::to_string(ino_out));
    buf.append("/");

    ec->put(parent, buf);

    return r;
}

int
chfs_client::lookup(inum parent, const char *name, bool &found, inum &ino_out)
{
    int r = OK;

    /*
     * your code goes here.
     * note: lookup file from parent dir according to name;
     * you should design the format of directory content.
     */
    std::list<dirent> l;
    readdir(parent, l);

    for (auto it = l.begin(); it != l.end(); it++) {
        if ((*it).name.compare(name) == 0) {
            found = 1;
            ino_out = (*it).inum;
            return r;
        }
    }

    found = 0;
    return r;
}

chfs_client::dirent::dirent() { }

chfs_client::dirent::dirent(std::string str) {
    size_t mid = str.find_last_of(':');
    if (mid == std::string::npos)
        {
            printf("name error");
            return;
        }
    this->name = str.substr(0, mid);
    this->inum = n2i(str.substr(mid+1, std::string::npos));
}

int
chfs_client::readdir(inum dir, std::list<dirent> &list)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should parse the dirctory content using your defined format,
     * and push the dirents to the list.
     */
    std::string buf;
    if (ec->get(dir, buf) != OK) {

    }

    size_t start = 0, end = buf.find('/');
    while(end != std::string::npos) {
        list.push_back(dirent(buf.substr(start, end - start)));
        start = end+1;
        end = buf.find('/', start);
    }

    return r;
}

int
chfs_client::read(inum ino, size_t size, off_t off, std::string &data)
{
    int r = OK;

    /*
     * your code goes here.
     * note: read using ec->get().
     */
    std::string buf;
    int rt = ec->get(ino, buf);
    if (rt != OK) {return rt;}

    if ((size_t)off >= buf.size()) {
        data = "";
        return r;
    }

    data = buf.substr(off, size);

    return r;
}

int
chfs_client::write(inum ino, size_t size, off_t off, const char *data,
        size_t &bytes_written)
{
    int r = OK;

    /*
     * your code goes here.
     * note: write using ec->put().
     * when off > length of original file, fill the holes with '\0'.
     */

    std::string buf;
    ec->get(ino, buf);

    std::string tmp;
    tmp.assign(data, size);

    if (off + size > buf.size()) buf.resize(size+off);

    buf.replace(off, size, tmp);
    ec->put(ino, buf);
    bytes_written = size;

    return r;
}

int chfs_client::unlink(inum parent,const char *name)
{
    int r = OK;

    /*
     * your code goes here.
     * note: you should remove the file using ec->remove,
     * and update the parent directory content.
     */
    bool found;
    inum tmp;

    assert(lookup(parent, name, found, tmp) == OK);
    ec->remove(tmp);
    

    std::string buf;
    assert(ec->get(parent, buf) == OK);

    size_t start = buf.find(name);
    size_t end = buf.find('/', start+1);

    buf.erase(buf.begin()+start, buf.begin()+end+1);

    ec->put(parent, buf);


    return r;
}

int chfs_client::symlink(inum parent, const char *name, const char* path, inum &ino_out) {
    bool found;
    inum tmp;

    assert(lookup(parent, name, found, tmp) == OK);
    if (found == 1) {
        printf("dir already exist!");
        return EXIST;
    }

    std::string buf;

    assert(ec->create(extent_protocol::T_SYMLINK, ino_out) == OK);
    ec->put(ino_out, path);

    assert(ec->get(parent, buf) == OK);

    buf.append(name);
    buf.append(":");
    buf.append(std::to_string(ino_out));
    buf.append("/");

    ec->put(parent, buf);

    return OK;
}

int chfs_client::readlink(inum ino, std::string &data) {

    std::string buf;
    ec->get(ino, buf);
    data = buf;
    return OK;
}

