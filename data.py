
import yfinance as yf
import pandas as pd
import time

# Mixing US and Indian stocks (NS = National Stock Exchange of India)
stocks = ["AAPL", "TSLA", "TCS.NS", "RELIANCE.NS", "HDFCBANK.NS"]
all_data = []

print("Initiating Historical Tick Download (1-Minute Intervals)...")
for ticker in stocks:
    print(f"Fetching {ticker}...")
    try:
        df = yf.download(ticker, period="7d", interval="1m", progress=False)
        
        if not df.empty:
            # 1. Create DataFrame and set Timestamps FIRST to establish the correct row count
            clean_df = pd.DataFrame()
            clean_df["TIMESTAMP"] = df.index  # Grab directly from index
            
            # 2. NOW assign the ticker, and Pandas will broadcast it to every row
            clean_df["TICKER"] = ticker
            
            # 3. Extract the 'Close' price
            if isinstance(df.columns, pd.MultiIndex):
                clean_df["PRICE"] = df["Close"].iloc[:, 0].values
            else:
                clean_df["PRICE"] = df["Close"].values
            
            all_data.append(clean_df)
        else:
            print(f"⚠️ No data found for {ticker}. It may be delisted or inactive.")
            
    except Exception as e:
        print(f"❌ Failed to download {ticker}: {e}")
        
    time.sleep(1)

# Combine, Sort, and Save
if all_data:
    print("\nMerging and structuring data for the C++ Exchange Server...")
    
    # Combine all individual stock DataFrames into one massive dataset
    final_df = pd.concat(all_data, ignore_index=True)
    
    # CRITICAL: Sort by TIMESTAMP. 
    # A real market feed happens chronologically across all stocks.
    final_df = final_df.sort_values(by="TIMESTAMP")
    
    # Save to CSV (dropping the Pandas index row numbers)
    filename = "market_data1.csv"
    final_df.to_csv(filename, index=False)
    # print(final_df.head())
    # print(final_df.columns)
    
    total_rows = len(final_df)
    print(f"✅ Success! Saved {total_rows} chronological ticks to {filename}.")
else:
    print("\n❌ Critical Failure: No data was fetched. Check your network.")