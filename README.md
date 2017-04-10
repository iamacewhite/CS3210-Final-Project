6.828-paging-system-project
===========================

Final project (implement a paging system for JOS) for MIT 6.828, Fall 2012

Group members: Gurtej Kanwar (gurtej@mit.edu, @gkanwar), Zixiao Wang (garywang@mit.edu, @Garywang), Jordan Moldow (jmoldow@mit.edu, @jmoldow)

Group 2


Project Title: Paging System

Project Write-up: In README.pdf

-----------------
Project Proposal:

We would like to implement the paging system (swapping memory pages in and out of disk) for our final project. One of the reasons this particular project was interesting was the possibility of developing an interesting page swapping heuristic to choose optimal pages to swap. There exist some heuristics for these already, such as demand paging, anticipatory paging, etc. We would try to implement a few of these as well as potentially constructing some heuristics of our own. Based on the performance of these, we would try to refine our solutions and ultimately pick one as the final paging heuristic.

In order to test the paging heuristics we would also have to develop a user program that returns some sort of metrics. For this we plan to develop some program that forks several different processes that make varying use of memory. In theory these processes should simulate common memory usage situations so that our heuristic would be optimized for the common cases. We would also try to think of some edge cases that could potentially cause thrashing to test worst case performance.

One tangential consideration is the process scheduler. The pages that are about to be used in the near future depend heavily on which process in user space gets scheduled next. As such, one possible addition to the project would be modifying the existing scheduler and/or paging heuristic to work well together and best choose pages to swap out/in.

-----------------

