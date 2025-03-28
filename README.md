Wish GPS - Road Information and Client Tracking System
======================================================

Description
-----------
Wish GPS is a multi-threaded client-server application designed to simulate a real-time road information and vehicle tracking system. The server maintains a database of roads, traffic conditions, accidents, weather reports, and provides personalized updates to clients regarding speed recommendations, gas station locations, and sports news.

This application allows clients to interactively report their location, current speed, and request various real-time data.

Features
--------
- Real-Time Traffic Information: Clients receive updates on nearby accidents, recommended speeds, and weather conditions.
- Accident Reporting: Clients can report accidents, triggering alerts to all affected users within proximity.
- Gas Station Locator: Provides information about the nearest gas stations and current fuel prices.
- Sports Updates: Randomly generated sports news provided for entertainment.
- Road Management: Add and manage roads dynamically within the system.
- Client Location Tracking: Clients provide their current location, allowing personalized feedback.

Project Structure
-----------------
project/
├── server.c
├── client.c
├── trafic.db (automatically generated SQLite database)
└── README.txt

Requirements
------------
- Linux Operating System
- GCC Compiler
- SQLite3 Library (development headers required)
- POSIX Threads (pthread)

Installation
------------
1. Ensure you have SQLite3 and pthread libraries installed. On Debian-based systems, use:
   sudo apt install sqlite3 libsqlite3-dev build-essential

2. Compile the Server:
   gcc server.c -o server -lsqlite3 -lpthread

3. Compile the Client:
   gcc client.c -o client -lpthread

Running the Application
-----------------------
Starting the Server:
./server

Starting Clients:
./client <server_ip> <port>

Example:
./client 127.0.0.1 2728

Commands Available to Clients
-----------------------------
- locatie <road_name> <km_marker> <speed>  
  Sets your current location and speed.
- accident  
  Reports an accident at your current location.
- viteza <speed>  
  Updates your current driving speed.
- insert_drum <road_name> <road_type>  
  Adds a new road to the database. Types: autostrada, drum, oras.
- show_drums  
  Lists all roads in the system.
- CL#ARCR4SH  
  Clears all accident reports from the system.
- gas  
  Provides distance to the nearest gas station and current fuel prices.
- weather  
  Displays weather conditions for your current road.
- sports  
  Shows a random sports news headline.
- AllInfo  
  Combines gas, weather, and sports information into one report.
- help  
  Shows this list of available commands.
- exit  
  Disconnects from the server.

Usage Example
-------------
1. Connect to the server:
   ./client 127.0.0.1 2728

2. Register your current location and speed:
   locatie DN1 35 80

3. Report an accident:
   accident

4. Request gas station information:
   gas

Database Schema (SQLite)
------------------------
Drumuri Table:
Name (PK) | Type | Neighbours | IntersectionPointNeighbour | NeighbourNumber | TotalKms | GasStation | GasPrices | Crashes | Weather | Speed

Clienti Table:
clientid (PK) | NameRoad | LocationKm | Speed | GasInfo | WeatherInfo | SportsInfo

Technical Notes
---------------
- Server handles multiple client connections using POSIX threads.
- All database operations are thread-safe, managed via mutex locks.
- Client application employs asynchronous reads to remain responsive to server updates.
