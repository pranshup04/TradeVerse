import zmq
import sys

def run_chat_client():
    context = zmq.Context()
  
    chat_socket = context.socket(zmq.REQ)
    chat_socket.connect("tcp://127.0.0.1:5556")

    print("+----------------------------------------------------------+")
    print("|  TradeVerse -- Command Terminal                           |")
    print("|  Connected to C++ Risk Engine on Port 5556               |")
    print("+----------------------------------------------------------+")
    print()
    print("Available commands:")
    print("  FETCH:<TICKER>       -- Get price & volume  (e.g. FETCH:AAPL)")
    print("  BUY:<TICKER>:<QTY>   -- Buy shares          (e.g. BUY:AAPL:100)")
    print("  SELL:<TICKER>:<QTY>  -- Sell shares          (e.g. SELL:TSLA:50)")
    print("  GREEKS:<TICKER>      -- Get live Greeks      (e.g. GREEKS:TSLA)")
    print("  PORTFOLIO            -- Portfolio-level Greeks + latency")
    print("  STATUS_CHECK         -- Engine health check")
    print("  MANUAL_OVERRIDE      -- Emergency protocol")
    print("-" * 58)

    try:
        while True:
       
            user_command = input(">> ").strip()
            
            if not user_command:
                continue
                
            if user_command.lower() == 'exit':
                print("Closing command terminal.")
                break

            # Send the string command to C++
            chat_socket.send_string(user_command)
            
            # Receive the mandatory confirmation back from C++
            reply = chat_socket.recv_string()
            print(f"[Server]: {reply}\n")
            
    except KeyboardInterrupt:
        print("\nExiting command terminal.")
    finally:
        chat_socket.close()
        context.term()

if __name__ == "__main__":
    run_chat_client()