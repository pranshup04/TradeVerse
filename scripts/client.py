import zmq

# 1. Connect to the ZeroMQ data broadcast highway
context = zmq.Context()
subscriber = context.socket(zmq.SUB)

# Connect to the local loopback address for same-device testing
subscriber.connect("tcp://127.0.0.1:5555")

# Subscribe to everything (both market data and Greek broadcasts)
subscriber.setsockopt_string(zmq.SUBSCRIBE, "")

print("+----------------------------------------------------------+")
print("|  TradeVerse -- Live Risk Dashboard Client                 |")
print("|  Listening for Market Data + Greek Broadcasts             |")
print("+----------------------------------------------------------+")
print()

while True:
    # Capture the raw network string frame
    raw_packet = subscriber.recv_string()
    data_list = raw_packet.split(',')

    # GREEK BROADCAST: "GREEKS,TICKER,Delta,Gamma,Vega,Theta,Rho"
    if data_list[0] == "GREEKS" and len(data_list) == 7:
        ticker = data_list[1]
        delta  = float(data_list[2])
        gamma  = float(data_list[3])
        vega   = float(data_list[4])
        theta  = float(data_list[5])
        rho    = float(data_list[6])

        print(f"  GREEKS | {ticker:>12s} | Delta={delta:>12.4f} | Gamma={gamma:>10.4f} | Vega={vega:>10.4f} | Theta={theta:>10.4f} | Rho={rho:>10.4f}")

    # MARKET DATA: "TICKER,TIMESTAMP,PRICE,VOLUME"
    elif len(data_list) == 4:
        ticker    = data_list[0]
        timestamp = data_list[1]
        price     = data_list[2]
        volume    = data_list[3]

        print(f"  TICK   | {ticker:>12s} | {timestamp} | Price: {price} | Vol: {volume}")

    # LEGACY FORMAT: "TICKER,TIMESTAMP,PRICE" (3 fields, no volume)
    elif len(data_list) == 3:
        ticker     = data_list[0]
        timestamp  = data_list[1]
        spot_price = float(data_list[2])

        print(f"  TICK   | {ticker:>12s} | {timestamp} | Price: ${spot_price:.2f}")

    else:
        print(f"  WARN   | Dropped malformed packet: {raw_packet}")