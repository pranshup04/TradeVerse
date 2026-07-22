import zmq
import sys

def run_chat_client():
    context = zmq.Context()
  
    chat_socket = context.socket(zmq.REQ)
    chat_socket.connect("tcp://127.0.0.1:5556")

    print("💬 Shaurya's Command Terminal Connected to C++ Server over Port 5556.")
    print("Available test commands: 'STATUS_CHECK', 'MANUAL_OVERRIDE', or any text logs.")
    print("-" * 65)

    try:
        while True:
       
            user_command = input("Enter command to send (or 'exit' to quit): ").strip()
            
            if not user_command:
                continue
                
            if user_command.lower() == 'exit':
                print("Closing command terminal.")
                break

            # 1. Send the string command to C++
            chat_socket.send_string(user_command)
            
            # 2. Receive the mandatory confirmation back from C++
            reply = chat_socket.recv_string()
            print(f"📡 [C++ Server Response]: {reply}\n")
            
    except KeyboardInterrupt:
        print("\nExiting command terminal.")
    finally:
        chat_socket.close()
        context.term()

if __name__ == "__main__":
    run_chat_client()