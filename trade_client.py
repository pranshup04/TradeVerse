import zmq
import sys

def main():
    # Initialize ZeroMQ context
    context = zmq.Context()
    
    # Connect to the C++ ROUTER-DEALER Control Plane on Port 5556
    print("🔌 Connecting to C++ Trading Engine on port 5556...")
    socket = context.socket(zmq.REQ)
    socket.connect("tcp://localhost:5556")
    print("✅ Connected! System ready for high-speed trading commands.\n")

    print("==================================================")
    print("  AVAILABLE COMMANDS:")
    print("  - FETCH:<TICKER>     (e.g., FETCH:AAPL)")
    print("  - BUY:<TICKER>:<QTY>  (e.g., BUY:AAPL:10)")
    print("  - SELL:<TICKER>:<QTY> (e.g., SELL:TSLA:5)")
    print("  - STATUS_CHECK")
    print("  - exit")
    print("==================================================\n")

    while True:
        try:
            # Prompt user for input
            command = input("Trade Terminal > ").strip()
            
            if not command:
                continue
                
            if command.lower() == "exit":
                print("Exiting trading terminal...")
                break

            # Send trading or query command over TCP to C++ backend
            socket.send_string(command)

            # Wait for response from C++ worker thread
            response = socket.recv_string()
            print(f"📩 [Server Response]: {response}\n")

        except KeyboardInterrupt:
            print("\nTerminated by user.")
            break
        except Exception as e:
            print(f"❌ Communication Error: {e}")
            break

    # Clean up network sockets
    socket.close()
    context.term()

if __name__ == "__main__":
    main()