#!/bin/bash

#Do not insert code here

#DO NOT REMOVE THE FOLLOWING TWO LINES
git add $0 >> .local.git.out
git commit -a -m "Lab 2 commit" >> .local.git.out
git push >> .local.git.out || echo

# Function to print usage
usage() {
    echo "Usage: $0 <dir> <backup-dir> <max-backups>"
    exit 1
}

# Check if required arguments are provided
if [ $# -ne 3 ]; then
    usage
fi

# Assign arguments to variables
SOURCE_DIR="$1"
BACKUP_DIR="$2"
MAX_BACKUPS="$3"

# Check if source directory exists
if [ ! -d "$SOURCE_DIR" ]; then
    echo "Error: Source directory '$SOURCE_DIR' does not exist."
    exit 1
fi

# Check if max backups is a number
if ! [[ "$MAX_BACKUPS" =~ ^[0-9]+$ ]]; then
    echo "Error: max-backups should be a positive number."
    exit 1
fi

# Create backup directory if it doesn't exist
mkdir -p "$BACKUP_DIR"

# Determine next sequence number
SEQ_NUM=0
while [ -d "$BACKUP_DIR/$(basename "$SOURCE_DIR").$SEQ_NUM" ]; do
    SEQ_NUM=$((SEQ_NUM + 1))
done

# Get the latest backup directory if exists
LATEST_BACKUP="$BACKUP_DIR/$(basename "$SOURCE_DIR").$((SEQ_NUM - 1))"

# Check if a backup is needed
if [ -d "$LATEST_BACKUP" ] && diff -qr "$SOURCE_DIR" "$LATEST_BACKUP" > /dev/null 2>&1; then
    echo "No backup necessary"
    exit 0
fi

# Create new backup
NEW_BACKUP="$BACKUP_DIR/$(basename "$SOURCE_DIR").$SEQ_NUM"
cp -r "$SOURCE_DIR" "$NEW_BACKUP"
echo "Backup created at: $NEW_BACKUP"

# Delete oldest backup if exceeding max backups
TOTAL_BACKUPS=$(ls -d "$BACKUP_DIR/$(basename "$SOURCE_DIR")."* 2>/dev/null | wc -l)
if [ "$TOTAL_BACKUPS" -gt "$MAX_BACKUPS" ]; then
    OLDEST_BACKUP=$(ls -d "$BACKUP_DIR/$(basename "$SOURCE_DIR")."* | head -n 1)
    rm -rf "$OLDEST_BACKUP"
    echo "Oldest backup removed: $OLDEST_BACKUP"
fi
