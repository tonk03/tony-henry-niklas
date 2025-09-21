# The Report
## Have you met all the specifications outlined for the lab?
Yes, we have met all the specifications outlined for this lab.

### In which order?
We started with just running single arguments using the ```execvp()``` function. From here we added the ability to redirect from ```STDOUT``` and ```STDIN```, as well as the ability to

After redirects, work began on blocking ```ctrl+C``` in the foreground thread using a signal handler, while still allowing background processes to exit. ```ctrl+D``` was implemented in the foreground process, by placing our parent process into its own process group.

Pipes were implemented last.

## What challenges did you encounter in meeting each specification?
It went pretty smooth for the most part, as the ```Command``` data structure stored and parsed a lot of the necessary data into easily extractable formats.

We had some issues with the auto-grader and colored writes to the terminal, which made all tests fail. After removing colored writes, all tests except piping related tests succedded.

For some of us, C knowledge was quite rusty, leading to some struggles with interacting with data structures, primarily lists.

Pipes:
Struggled a bit with the order of how to open and close our pipes. Was also confusing on if we should start children from left-hand-side or right-hand-side. We decided with going from right-hand-side to left-hand-side. The inspiration came from the book *Advanced Programming in the UNIX Environment* where apparantly the first typed command should be exec'ed in the first fork after it forked processes for the other commands. Thats why we fork once and then fork again for command  in the pgmlist with increasing order (which is in reverse). This makes the last command in the list the first command typed into the shell, which then will be execed in the first fork.



## Do you have any feedback for improving the lab materials?
We think the lab material was quite good.

### Did you find the automated tests useful?
Yes, the automated tests were useful, although we encountered some issues when placing our shell into its own process group and making it the leader, as certain tests no longer functioned as expected. For most of the development process, we relied on manual testing by directly interacting with the shell, and then used the automated tests at the end as a final validation step to ensure correctness. One particular test, which evaluates the handling of `SIGINT` in a child process, revealed a race condition: the signal was delivered faster than our initial implementation could update the handler. This made us aware of the importance of minimizing race conditions, for example by configuring signal handling before forking a child process. To address this, we implemented a signal handler that manages signals under specific conditions and ensured that `tcsetpgrp` was invoked in both the child and parent processes, preventing forks from making assumptions about process states that are not guaranteed.
``````

