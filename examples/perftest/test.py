from selenium import webdriver
import time
import matplotlib.pyplot as plt
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.common.by import By
import numpy as np

# Set up the Chrome driver
driver = webdriver.Chrome()

# # Navigate to the website and start the timer
# start_time = time.time()
# driver.get("http://192.168.86.75/")

# # Wait for the page to load
# driver.implicitly_wait(10)

# # Stop the timer and calculate the elapsed time
# end_time = time.time()
# elapsed_time = end_time - start_time

# # Print the elapsed time
# print("Elapsed time: {:.2f} seconds".format(elapsed_time))

# Set up the loop variables
num_tests = 100
times = []
successes = []

# Run the tests in a loop
for i in range(num_tests):
    # Navigate to the website and start the timer
    start_time = time.time()
    driver.get("http://192.168.86.75/")

    # Wait for the page to load
    # driver.implicitly_wait(10)

    # wait = WebDriverWait(driver, 10)

    # Wait for the websocket to connect and the content-status field to be "WebSocket is connected!"
    success = False
    try:
        elem = WebDriverWait(driver, 5).until(EC.text_to_be_present_in_element((By.ID, "webSocketState"), "WebSocket is connected!"))
        if elem:
            # print(f"elem: {elem}")
            success = True
        else:
            print("elem is None")
    except Exception as e:
        print(f"Exception: {e}")

    # # Check if the page loaded successfully
    # success = "WebSocket is connected!" in driver.page_source

    # Stop the timer and calculate the elapsed time
    end_time = time.time()
    elapsed_time = end_time - start_time

    # Record the results
    times.append(elapsed_time)
    successes.append(success)

# Use numpy to calculate the success rate
success_rate = np.sum(successes) / num_tests

# Use numpy to calculate the average time and standard deviation
average_time = np.mean(times)
std_dev = np.std(times)

# Print the results
print(f"Success rate: {success_rate * 100:.2f}%")
print(f"Average time: {average_time:.2f} seconds")
print(f"Standard deviation: {std_dev:.2f} seconds")

# # Plot the results
# plt.plot(times)
# plt.title("Website Load Times")
# plt.xlabel("Test Number")
# plt.ylabel("Load Time (s)")
# plt.show()

# Close the browser
driver.quit()
