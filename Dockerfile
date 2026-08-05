FROM gcc:12

# Install Python 3
RUN apt-get update && apt-get install -y python3 python3-pip && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . /app

# Compile TigerFish engine binary for Linux with C++20 standard & maximum optimization
RUN g++ -O3 -std=c++20 -o game engine/main.cpp

# Make game binary executable
RUN chmod +x game

# Run Lichess Bot Bridge
CMD ["python3", "lichess_bot.py"]
