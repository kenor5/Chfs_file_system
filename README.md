# Lab 1: Basic File System

lab1要求实现一个基础的文件系统

第一部分要求实现inode manager

第二部分要求实现一些基本的operation如get put create setattr等

架构图：
![架构图](https://ipads.se.sjtu.edu.cn/courses/cse/labs/lab1-assets/part1.png)


# Lab 2A: Atomicity
Lab2A要求在单体文件系统中实现crash consistency

第一部分使用基本的log机制来持久化数据和操作

第二部分使用checkpoint减少log文件大小

# Lab 2B: RPC and Lock Server
Lab2B要求实现在并发场景下的一致性

第一部分使用RPC实现一个server

第二部分实现一个locker server

第三部分使用2PL保证并发操作正确

![架构图](https://ipads.se.sjtu.edu.cn/courses/cse/labs/lab2b-assets/lab2b-2.png)

# Lab 3: Fault-tolerant Distributed Filesystem with Raft

Lab3要求使用Raft协议实现容错的分布式文件系统

第一部分要实现leader选举和心跳机制

第二部分实现log备份机制

第三部分实现持久化日志

第五部分用raft协议拓展lab1中的单体文件系统

![架构](https://ipads.se.sjtu.edu.cn/courses/cse/labs/lab3-assets/part5.png)
