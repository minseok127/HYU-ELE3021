Upgraded xv6   
[HYU-ELE3021, 1st semester 2021]
================================
운영체제는 동시에 돌아가는 다수의 프로세스들에게 컴퓨터의 자원을 적절히 분배해야합니다.    
   
본 프로젝트에서는 xv6 운영체제를 기반으로 이와 관련된 새로운 기능을 구현합니다.   
   
Contents
========
### 1. MLFQ and Stride Scheduling   
OS는 여러 프로세스들이 CPU를 올바르게 점유할 수 있도록 기능을 제공해야 합니다.   
   
xv6에서는 기본적으로 Round Robin Scheduling을 사용하지만   
   
이를 발전시켜 MLFQ Scheduling과 Stride Scheduling을 결합한 Scheduler로 구현합니다.
   
* [MLFQ and Stride Scheduling Design](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/MLFQ-and-Stride-Scheduling-Design)
* [MLFQ and Stride Scheduling Implement](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/MLFQ-and-Stride-Scheduling-Implement)
   
### 2. Light Weight Process   
서로 다른 프로세스는 Address space나 File descriptor 등과 같은 시스템 자원을 독립적으로 가지고 있습니다.   
   
이와 다르게 Light-Weight-Process(LWP)는 같은 프로세스에 속한 LWP끼리 시스템 자원을 공유합니다.   
   
이번 프로젝트에서는 Light Weight Process를 구현합니다.   
    
* [Basic Design of LWP Operations](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/Basic-Design-of-LWP-Operations)
* [Implementation of Basic LWP Operations](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/Implementation-of-Basic-LWP-Operations)   
* [Interactions with System calls in xv6](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/Interactions-with-System-calls-in-xv6)
* [Interacitons with Scheduler in xv6](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/How-to-Schedule-Threads%3F)   
   
또한 LWP의 디자인 및 구현과 별개로 Time Quantum에 대한 해석이 project1과 달라진 부분이 있기 때문에   
   
Process의 Scheduling 방식에도 변화가 생겼습니다.     
   
* [Modified Scheduler](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/Modified-Scheduler)   
   
### 3. File System   
File System은 Data의 Persistence를 제공하기 위해 필요한 개념입니다.   
   
이번 프로젝트에서는 xv6가 기존에 가지고 있던 File System을 보완합니다.  
   
   
첫 번째로 write의 결과를 buffer에 보관하여 속도를 높이고 buffer와 disk를 동기화시키는 sync를 구현합니다.    
    
* [Basic Design of write system call with cache](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/Basic-Design-of-write-system-call-with-cache)
* [Implementation of new write system call](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/Implementation-of-new-write-system-call)
* [Implementation of sync system call](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/Implementation-of-sync-system-call)   
   
두 번째로 file의 size를 확장시킵니다.   
   
* [File with Blocks](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/How-to-store-file-on-disk%3F)   
* [Triple Indirect Block](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/More-than-single-indirect-block)
   
세 번째로 pread와 pwrite를 구현합니다.   
   
* [pread](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/pread)
* [pwrite](https://github.com/minseok127/OS-with-xv6-ELE3021/wiki/pwrite)
