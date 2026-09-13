# SP

# Concurrent Stock Server

## 1. Project Overview

This project implements a concurrent stock server that can handle multiple clients simultaneously.

Two different concurrency models are implemented:

* **Task 1:** Event-driven server using `select()`
* **Task 2:** Thread-based server using `pthread`

The server manages stock information and supports multiple client requests such as viewing, buying, and selling stocks.

## 2. Features

The server supports the following commands:

* `show`
  Displays all available stocks.

* `buy [stock ID] [amount]`
  Purchases the specified amount of stock.
  If there is not enough stock available, the server returns `Not enough left stocks`.

* `sell [stock ID] [amount]`
  Adds the specified amount to the available stock.

* `exit`
  Disconnects the client from the server.

## 3. Task 1 - Event-driven Server

Task 1 implements concurrency using the `select()` system call.

The server monitors multiple client file descriptors and processes requests from multiple connected clients using I/O multiplexing.

### Build

```bash
cd task1
make
```

### Run

Start the server:

```bash
./stockserver [port]
```

Example:

```bash
./stockserver 1119
```

Run a client:

```bash
./stockclient [server IP] [port]
```

## 4. Task 2 - Thread-based Server

Task 2 implements concurrency using the `pthread` library.

A master thread accepts incoming client connections and places their file descriptors into a shared buffer. Worker threads retrieve connections from the buffer and process client requests concurrently.

### Build

```bash
cd task2
make
```

### Run

```bash
./stockserver [port]
```

The client can be executed in the same way as Task 1.

## 5. Stock Data

Stock information is stored in `stock.txt`.

Each stock contains:

```text
stock_ID remaining_stock price
```

When the server starts, the stock information is loaded from `stock.txt`.

When the server is terminated using `SIGINT`, the current stock information is written back to `stock.txt`.

## 6. Multi-client Test

`multiclient` can be used to test the server with multiple concurrent clients.

```bash
./multiclient [server IP] [port] [number of clients]
```

Example:

```bash
./multiclient [server IP] 1119 4
```

## 7. Project Structure

```text
StudentID/
├── document.pdf
├── task1/
│   ├── Makefile
│   ├── csapp.c
│   ├── csapp.h
│   ├── echo.c
│   ├── multiclient.c
│   ├── stockclient.c
│   ├── stockserver.c
│   └── stock.txt
└── task2/
    ├── Makefile
    ├── csapp.c
    ├── csapp.h
    ├── echo.c
    ├── multiclient.c
    ├── stockclient.c
    ├── stockserver.c
    └── stock.txt
```

## 8. Environment

* Language: C
* Platform: Linux
* Concurrency:

  * Task 1: `select()`
  * Task 2: POSIX Threads (`pthread`)
* Build: `make`

The project should be compiled and tested on the CSPRO server.
