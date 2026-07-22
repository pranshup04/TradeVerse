import zmq

# 1. Connect to the ZeroMQ pipeline highway
context = zmq.Context()
subscriber = context.socket(zmq.SUB)

# Connect to the local loopback address for same-device testing
subscriber.connect("tcp://127.0.0.1:5555")

# Subscribe to everything
subscriber.setsockopt_string(zmq.SUBSCRIBE, "")

print("Shaurya's Python Predictive Engine running and listening...")

while True:
    # 2. Capture the raw network string frame
    raw_packet = subscriber.recv_string() 
    
    # 3. Split the string by commas into a Python list
    data_list = raw_packet.split(',')
    
    # Safeguard: Ensure the packet actually has 3 components before processing
    if len(data_list) == 3:
        # 4. Map the index values to the NEW CSV format: TICKER, TIMESTAMP, PRICE
        ticker = data_list[0]               # Index 0: String ("RELIANCE")
        timestamp = data_list[1]            # Index 1: String ("2026-07-08 09:15:00+05:30")
        spot_price = float(data_list[2])    # Index 2: Convert string to Float
        
        # 5. Print your parsed values cleanly to the terminal
        print(f"Parsed Stream -> Asset: {ticker} | Time: {timestamp} | Price: ₹{spot_price:.2f}")
    else:
        print(f"⚠️ Dropped malformed packet: {raw_packet}")