
import yfinance as yf
import pandas as pd
import time

# Mixing US and Indian stocks (NS = National Stock Exchange of India)
# We use .NS suffix for yfinance download but strip it for internal consistency
stocks = ["AAPL", "TSLA", "TCS.NS", "RELIANCE.NS", "HDFCBANK.NS"]
all_data = []

# Default volume for each ticker (simulated tradeable volume)
DEFAULT_VOLUME = 1000

print("Initiating Historical Tick Download (1-Minute Intervals)...")
for ticker in stocks:
    print(f"Fetching {ticker}...")
    try:
        df = yf.download(ticker, period="7d", interval="1m", progress=False)
        
        if not df.empty:
            clean_df = pd.DataFrame()
            clean_df["Date"] = df.index

            # Strip .NS suffix for internal consistency with portfolio
            clean_ticker = ticker.replace(".NS", "")
            clean_df["Ticker"] = clean_ticker
            
            # Extract the 'Close' price
            if isinstance(df.columns, pd.MultiIndex):
                clean_df["Price"] = df["Close"].iloc[:, 0].values
            else:
                clean_df["Price"] = df["Close"].values

            # Add default tradeable volume
            clean_df["Volume"] = DEFAULT_VOLUME
            
            all_data.append(clean_df)
        else:
            print(f"WARNING: No data found for {ticker}. It may be delisted or inactive.")
            
    except Exception as e:
        print(f"FAILED: Could not download {ticker}: {e}")
        
    time.sleep(1)

# Combine, Sort, and Save
if all_data:
    print("\nMerging and structuring data for the C++ Exchange Server...")
    
    # Combine all individual stock DataFrames into one massive dataset
    final_df = pd.concat(all_data, ignore_index=True)
    
    # Sort by Date (chronological market order)
    final_df = final_df.sort_values(by="Date")
    
    # Keep only the LATEST entry per ticker (server loads into a map, not a stream)
    # This gives us one row per ticker with the most recent price
    latest_df = final_df.groupby("Ticker").tail(1).reset_index(drop=True)
    
    # Save to CSV
    filename = "data/market_data1.csv"
    latest_df.to_csv(filename, index=False)
    
    total_rows = len(latest_df)
    unique_tickers = latest_df["Ticker"].nunique()
    print(f"Success! Saved {total_rows} tickers ({unique_tickers} unique) to {filename}.")
    print(latest_df.to_string(index=False))
else:
    print("\nCRITICAL: No data was fetched. Check your network.")