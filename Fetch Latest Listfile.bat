@echo off

echo Deleting old listfile.csv if present.
del listfile.csv

echo Fetching latest listfile from https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv.
curl -LO "https://github.com/wowdev/wow-listfile/releases/latest/download/community-listfile.csv"

echo renaming community-listfile.csv to listfile.csv
ren community-listfile.csv listfile.csv