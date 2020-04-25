import datetime
from datetime import timedelta
import re

#read the file line by line
file1 = open("time_line_of_4_process.txt",'r')
lines = file1.readlines()

timeregex = re.compile(r'\d\d:\d\d:\d\d')
start = 0
first_time = None
timeline_seconds = []
for line in lines:
    time_string = timeregex.search(line)
    if time_string is not None:
        time1 = datetime.datetime.strptime(time_string.group(), "%H:%M:%S")
        #print(time1.time())
        if start == 0:
            first_time = time1

        start = start+1
        time_del = time1-first_time
        #print time delta as seconds
        #print(time_del.total_seconds())
        timeline_seconds.append(time_del.total_seconds())


plot_yaxis = [1]*len(timeline_seconds)
temp_prev = None
total_idle = 0
#now check the time one by one to find the gaps.
#gaps larger than 30 seconds should be
for i,times in enumerate(timeline_seconds):
    if i == 0:
        temp_prev = times
        continue
    if (times-temp_prev)>15:
        #candidate for ideal time
        plot_yaxis[i] = 0
        plot_yaxis[i-1] = 0
        total_idle  = total_idle+(times-temp_prev)
        print (times-temp_prev)
    temp_prev = times
    
#print(plot_yaxis)

print("Total Time (sec) : ",(timeline_seconds[-1]-timeline_seconds[0]))
print("Total Idle time (sec): ", total_idle)
