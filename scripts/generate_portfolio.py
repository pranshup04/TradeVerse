import pandas as pd
import numpy as np

# 1. Configuration
num_positions = 10000
tickers = ["AAPL", "TSLA", "TCS", "RELIANCE", "HDFCBANK"]
option_types = ["Call", "Put", "Stock"]

print("🎲 Generating 10,000 randomized hedge fund positions...")

# 2. Generate randomized matrix data
data = {
    "TICKER": np.random.choice(tickers, num_positions),
    "POSITION_SIZE": np.random.randint(100, 5000, num_positions) * np.random.choice([-1, 1], num_positions), # Negative means Short/Sold
    "STRIKE_PRICE": np.random.randint(200, 1500, num_positions),
    "EXPIRY_DAYS": np.random.randint(7, 90, num_positions),
    "OPTION_TYPE": np.random.choice(option_types, num_positions, p=[0.4, 0.4, 0.2]) # 80% Options, 20% Underlying Stock
}

# 3. Structure into DataFrame
portfolio_df = pd.DataFrame(data)

# Clean up strike prices and expiries for plain stocks (stocks don't have strike/expiry)
portfolio_df.loc[portfolio_df["OPTION_TYPE"] == "Stock", ["STRIKE_PRICE", "EXPIRY_DAYS"]] = 0

# 4. Save to Disk
portfolio_df.to_csv("data/portfolio.csv", index=False)
print("✅ Success! Created 'portfolio.csv' with 10,000 positions.")
print(portfolio_df.head())