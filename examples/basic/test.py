# Test program to get web page repeatedly and test timing
# of the web server.

import sys
import time
import requests
import json
import argparse

# Parse command line arguments
parser = argparse.ArgumentParser(description='Test web server.')
parser.add_argument('--url', dest='url', action='store', default='http://localhost:80',
                    help='URL to test')
parser.add_argument('--count', dest='count', action='store', default=100, type=int,
                    help='Number of times to test')

args = parser.parse_args()

# Get the URL to test
url = args.url

# Check if the URL starts with http://
if not url.startswith("http://"):
    url = "http://" + url

# Get the number of times to test
count = args.count

# Print the test parameters
print(f"Testing {url} {count} times")

# Time of start of test
start = time.time()

# Test the URL
for i in range(count):

    # Get the web page
    response = requests.get(url)

    # Check the response code
    if response.status_code != 200:
        print(f"Error: {response.status_code} {response.reason}")
        sys.exit(1)

# Time of end of test
end = time.time()

# Calculate the total time
total = end - start

# Calculate the average time
average = total / count

# Print the results
print(f"Average time: {average*1000:.0f} ms")
