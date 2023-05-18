# IPC: Inter-Process Communication

The getfile server act as a proxy server. It accepts incoming GETFILE requests and translate them into http requests for another server, such as one located on the internet.

The cache process runs on the same machine as the proxy and communicate with it via shared memory.

![part2 architecture](docs/part2.png)


- **gfclient_download** - a binary executable that serves as a workload generator for the proxy.  It downloads the requested files, using the current directory as a prefix for all paths. 

- **gfclient_measure** - a binary executable that serves as a workload generator for the proxy.  It measures the performance of the proxy, writing one entry in the metrics file for each chunk of data received. Note you must specify the correct port to use.

- **gfserver.h** - header file for the library that interacts with the getfile client.

- **gfserver.o** - object file for the library that interacts with the getfile client.

- **handle_with_curl.c** - On a 404 from the webserver, this function returns a header with a Getfile status of `GF_FILE_NOT_FOUND`, as normal web servers map access denied and not found to the same error code to prevent probing security attacks.

- **handle_with_cache.c** - this function uses IPC mechanism to communicate with the simplecached process to obtain the file contents. 

- **simplecached.c** - the main file for the cache daemon process, which receives requests from the proxy and serve up the contents of the cache using the simplecache interface. The process uses a boss-worker multithreaded pattern, where individual worker threads are responsible for handling a single request from the proxy.

- **webproxy.c** - the main file for the webproxy program.  





