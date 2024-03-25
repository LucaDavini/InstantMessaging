# Instant Messaging Application

This application provides a real-time communication service through a hybrid architecture.  
The server is responsible for tracking clients and managing the conversations' set up. Once a conversation is started, communication continues directly between clients, even if the server is shut down.

## Features

- **Instant Messaging:** Real-time communication service.
- **Hybrid Architecture:** Combination of client-server and peer-to-peer architecture.
- **Server Management:** Server tracks online clients and manages conversation initiation.
- **Chat Functionalities:**
  - **Hanging Files:** Messages sent to an offline user are stored and recoverable once the user comes back online
  - **Group Chats:** Users can create and participate in group chat conversations.
  - **Sending Files:** Users can send files in a chat.
- **Offline functionality:** Communication between clients remains possible even if the server goes offline.

## Project Description

For detailed information about specific architectural decisions and features implemented in the application, refer to [ProjectDescription.pdf](ProjectDocumentation.pdf).

## 
*This application was developed as a Computer Networks exam project.*
