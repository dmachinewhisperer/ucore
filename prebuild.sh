#!/bin/bash

# Check if exactly two arguments are provided
if [ "$#" -ne 2 ]; then
    echo "Usage: $0 <src> <dst>"
    exit 1
fi

# Assign input arguments to variables
SRC=$1
DST=$2

# Check if source directory exists
if [ ! -d "$SRC/ports/esp32" ]; then
    echo "Source directory $SRC/ports/esp32 does not exist."
    exit 1
fi

# Create destination directory if it does not exist
mkdir -p "$DST"

# Copy all files matching "partitions*" from src/ports/esp32 to dst
cp "$SRC/ports/esp32/partitions"* "$DST"

# Check if copy was successful
if [ $? -eq 0 ]; then
    echo "Files copied successfully from $SRC/ports/esp32 to $DST"
else
    echo "Error during file copy."
    exit 1
fi
