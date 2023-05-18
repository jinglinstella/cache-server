# IPC: Inter-Process Communication

## Part 1

Implement the getfile server to act as a proxy server.
This server will accept incoming GETFILE requests and translate them into http requests for another server, such as one located on the internet.

![part1 architecture](docs/part1.png)

Here is a summary of the relevant files and their roles.  

- **gfclient_download** - a binary executable that serves as a workload generator for the proxy.  It downloads the requested files, using the current directory as a prefix for all paths. 

- **gfclient_measure** - a binary executable that serves as a workload generator for the proxy.  It measures the performance of the proxy, writing one entry in the metrics file for each chunk of data received. Note you must specify the correct port to use.

- **gfserver.h** - header file for the library that interacts with the getfile client.

- **gfserver.o** - object file for the library that interacts with the getfile client.

- **gfserver_noasan.o** - a non-address sanitizer version of **gfserver.o**.

- **handle_with_curl.c** - implement the handle_with_curl function here using the libcurl library.  On a 404 from the webserver, this function should return a header with a Getfile status of `GF_FILE_NOT_FOUND`, as normal web servers map access denied and not found to the same error code to prevent probing security attacks.

- **handle_with_file.c** - illustrates how to use the gfserver library with an example.

- **Makefile** - file used to compile the code.  Run `make` to compile your code.

- **webproxy.c** - this is the main file for the webproxy program.  

## Part 2

Implement a cache process that will run
on the same machine as the proxy and communicate with it via shared memory.

![part2 architecture](docs/part2.png)

- **gfclient_download** - a binary executable that serves as a workload generator for the proxy.  It downloads the requested files, using the current directory as a prefix for all paths. You must specify the correct port to use.

- **gfclient_measure** - a binary executable that serves as a workload generator for the proxy.  It measures the performance of the proxy, writing one entry in the metrics file for each chunk of data received. You must specify the correct port to use.

- **gfserver.h** - header file for the library that interacts with the getfile client.

- **gfserver.o** - object file for the library that interacts with the getfile client.

- **gfserver_noasan.o** - a version of **gfserver.o** created without [address sanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer) support. It may be used to test with [valgrind](http://www.valgrind.org/).

- **handle_with_cache.c** - implement the handle_with_cache function here.  It should use one of the IPC mechanisms discussed to communicate with the simplecached process to obtain the file contents. 

- **simplecached.c** - the main file for the cache daemon process, which should receive requests from the proxy and serve up the contents of the cache using the simplecache interface. The process should use a boss-worker multithreaded pattern, where individual worker threads are responsible for handling a single request from the proxy.

- **webproxy.c** - the main file for the webproxy program.  

