# Multi-Car Elevator Control System

A comprehensive elevator control system implementation in C for Linux, featuring multiple interconnected components that communicate via TCP/IP and POSIX shared memory. The system simulates a real-world elevator network with multiple cars, floor management, and safety protocols.

## System Components

The system consists of five main components:

1. **Car (car.c)**: Controls individual elevator car functionality
2. **Controller (controller.c)**: Central scheduling system that manages all elevator cars
3. **Call Pad (call.c)**: Simulates floor-level call buttons
4. **Internal Controls (internal.c)**: Simulates in-car controls and maintenance functions
5. **Safety System (safety.c)**: Monitors elevator conditions and manages emergency protocols

## Features

- Multi-car elevator scheduling and coordination
- Destination dispatch system
- Individual service mode for maintenance
- Emergency protocols and safety monitoring
- Floor-to-floor movement simulation
- Door operation simulation with obstruction detection
- TCP/IP based communication between components
- POSIX shared memory for internal car state management

## Technical Specifications

### Building the Project

The project uses a Makefile for compilation. To build:

```bash
# Build all components
make all

# Build individual components
make car
make controller
make call
make internal
make safety
```

### Component Usage

#### Car Component
```bash
./car {name} {lowest_floor} {highest_floor} {delay}
```
- `name`: Elevator car identifier (e.g., A, B, C)
- `lowest_floor`: Lowest accessible floor (e.g., B2, 1)
- `highest_floor`: Highest accessible floor (e.g., 10)
- `delay`: Operation timing in milliseconds

#### Controller Component
```bash
./controller
```
Runs on port 3000 and manages elevator scheduling

#### Call Pad Component
```bash
./call {source_floor} {destination_floor}
```
Simulates a user calling an elevator from one floor to another

#### Internal Controls Component
```bash
./internal {car_name} {operation}
```
Operations:
- `open`: Open doors
- `close`: Close doors
- `stop`: Emergency stop
- `service_on`: Enable service mode
- `service_off`: Disable service mode
- `up`: Move up one floor (service mode only)
- `down`: Move down one floor (service mode only)

#### Safety System Component
```bash
./safety {car_name}
```
Monitors elevator safety conditions and manages emergency protocols

## Architecture

### Communication Protocols

1. **TCP/IP Communication**
   - Used between cars and controller
   - Used between call pads and controller
   - Operates on localhost:3000
   - Uses length-prefixed message protocol

2. **Shared Memory**
   - Used between car, internal controls, and safety system
   - Named `/car{name}` for each car
   - Protected by POSIX mutex and condition variables

### Data Structures

#### Shared Memory Structure
```c
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    char current_floor[4];
    char destination_floor[4];
    char status[8];
    uint8_t open_button;
    uint8_t close_button;
    uint8_t door_obstruction;
    uint8_t overload;
    uint8_t emergency_stop;
    uint8_t individual_service_mode;
    uint8_t emergency_mode;
} car_shared_mem;
```

## Safety Features

- Door obstruction detection
- Emergency stop functionality
- Overload detection
- Data consistency checking
- Service mode for maintenance
- Floor range enforcement
- Automatic door operation
- Emergency mode protocols

## Operation Modes

### Normal Operation
- Automatic door operation
- Floor-to-floor movement
- Destination dispatch
- Queue-based scheduling

### Service Mode
- Manual door control
- Manual floor movement
- Disconnected from controller
- Maintenance access

### Emergency Mode
- Restricted operation
- Manual door control only
- No floor movement
- Safety system monitoring

## Requirements

- Linux operating system
- GCC compiler
- POSIX-compliant system
- TCP/IP networking support

## Implementation Notes

- All floor numbers are in the range B99 to 999
- Floor numbers increase going up (e.g., B2, B1, 1, 2, 3)
- Each car operates in its own shaft
- Destination dispatch system for efficient routing
- Cars can be configured for different floor ranges
- Implements industry-standard safety protocols
- MISRA C guidelines followed for safety-critical components

## Acknowledgments

This project was developed as part of the Systems Engineering unit at Queensland University of Technology.
