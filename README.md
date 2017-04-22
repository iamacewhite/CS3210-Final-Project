CS3210-Final-Project
===========================

Final project (extend paging system for JOS) for CS3210, Spring 2017

Team members: Chunyan Bai(cbai32@gatech.edu), Haoye Cai (hcai60@gatech.edu), Yanzhao Wang(ywang3254@gatech.edu)

Team 1


Project Title: Extend Paging System in JOS

Project Write-up: In README.pdf

-----------------
Project Proposal:

As we know, in JOS, the physical memory is limited to 256MB, which means the needed memory of a single process cannot exceed 256MB. One commonly implemented solution to the problem of limited memory is paging to disk. A paging system swaps memory pages between RAM and disk, allowing a process to use more memory than physically restricted. Since accessing permanent storage is much slower than accessing RAM, it is important for a good paging system to choose the right pages to swap out, or more specifically, to choose those pages that will rarely be used in the future. To accelerate this  swapping process, we will also modify the process scheduler so that it can coordinate with our paging system more smoothly.

Paging to disk is widely used in different operating systems nowadays, and it has become a crucial feature in order to break the memory limit. In Unix and other Unix-like operating systems, it is common to dedicate an entire partition of a hard disk to swapping. Many systems even have an entire hard drive dedicated to swapping, separate from the data drives, containing only a swap partition. In terms of determining pages to be swapped out, which is one of our main focus, there are some existing page swapping selection heuristics, such as demand paging, anticipatory paging, etc.

We aim to extend the functionality of paging system in JOS, implementing paging to disk so that virtual memory could exceed RAM . Furthermore, we intend to propose a novel paging heuristic in order to increase the performance of paging system, and also explore the influence of process scheduling policy on paging system.

Approaches:

1)Paging to Disk
Paging to disk is a rather simple process, given that it is common on modern operating systems. The idea is to modify the way we set up a new page, allowing construction of a new page even when RAM limit is reached, then replace some page in memory with the new page. When retrieving a page, we would look for the page in swap partition after page table is checked.

2)Paging Heuristic and Process Scheduling
Page heuristic here refer to the choice of choosing and evicting pages. The OS would benefit much from the performance gain of a good page heuristic. Therefore, we plan to come up with a new paging heuristic possibly with state-of-the-art algorithms, trying to outperform common systems by predicting the best pages to load or discard. This would be implemented in the function of loading page from disk. Another intuition is that process scheduling could affect paging system efficiency to a large extent, therefore we would be testing our module with multiple process scheduling algorithms, possibly including a novel one if time permits.

3)Performance Baseline
To make comparison between different combinations of page heuristic and process scheduling algorithms, we need to implement a user program which is in charge of triggering page faults, therefore we can compare performance with some metric. A common baseline to evaluate our paging heuristic would be an existing one, e.g. the one on xv6.


-----------------
To test out this project, simply run
'''
$ ./paging-stats
'''
