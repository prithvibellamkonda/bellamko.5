#Handling
For handling process requests two semaphores are used, one for the oss and other for processes.
Handling is run by the oss, exchanging messages with processes, more precisely writing to given
process's memory block.
The process can send 4 types of messages:
*TERMINATED: notify oss that it has decided to terminate
*REQUEST: notify oss that it is requesting resource $(process_block->request)
*RELEASE: notify oss that it is releasing all its resources
*HOLDING: waiting idly
The operating system can command the user process with the next messages:
*GRANTED: letting the process know it has been granted requested resource
*DENIED: letting the process know it has been denied requested resource
*TERMINATE: commanding the process to terminate, called by deadlock resolving algorithm

# Deadlock resolver
Once a process has requested a resource, it stays blocked until it is given requested resource, 
that's why only such processes, and ones requesting non-shared resources, can cause a deadlock, so 
only they are considered when solving.
Solving a deadlock is based on finding chains of dependencies, in other words, finding cycles.
This is explained in more detail in the source code itself.
Once such cycles are detected, it is enough to terminate once process from each cycle.
Have in mind that with current settings, in particular in user process simulations, specifically 
talking about termination, request and release chance of occurrences, as well as the rate of spawning 
new processes and the maximum number of them, there is a very small chance that system 
actually finds itself in a deadlock.


