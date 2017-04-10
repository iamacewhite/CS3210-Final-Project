CS3210-Final-Project
===========================

Final project (extend paging system for JOS) for CS3210, Spring 2017

Team members: Chunyan Bai(cbai32@gatech.edu), Haoye Cai (hcai60@gatech.edu), Yanzhao Wang(ywang3254@gatech.edu)

Team 1


Project Title: Extend Paging System in JOS

Project Write-up: In README.pdf

-----------------
Project Proposal:

As we know, in JOS, the physical memory is limited to 4GB, which means the needed memory of a single process cannot exceed 4GB. One commonly implemented solution to the problem of limited memory is paging to disk. A paging system swaps memory pages between RAM and disk, allowing a process to use more memory than physically restricted. Since accessing permanent storage is much slower than accessing RAM, it is important for a good paging system to choose the right pages to swap out, or more specifically, to choose those pages that will rarely be used in the future. To accelerate this  swapping process, we will also modify the process scheduler so that it can coordinate with our paging system more smoothly.

Paging to disk is widely used in different operating systems nowadays, and it has become a crucial feature in order to break the memory limit. In Unix and other Unix-like operating systems, it is common to dedicate an entire partition of a hard disk to swapping. Many systems even have an entire hard drive dedicated to swapping, separate from the data drives, containing only a swap partition. In terms of determining pages to be swapped out, which is one of our main focus, there are some existing page swapping selection heuristics, such as demand paging, anticipatory paging, etc.

We aim to extend the functionality of paging system in JOS, implementing paging to disk so that virtual memory could exceed RAM . Furthermore, we intend to propose a novel paging heuristic in order to increase the performance of paging system, and also explore the influence of process scheduling policy on paging system.
