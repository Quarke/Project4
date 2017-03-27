CONFIGURATION FILE PARAMETERS

PARTNERS: tflanag2 and dmattia

Parameter       Description                                                 Default
PERIOD_FETCH    The time (in seconds) between fetches of the various sites  180
NUM_FETCH       Number of fetch threads (1 to 8)                            1
NUM_PARSE       Number of parsing threads (1 to 8)                          1
SEARCH_FILE     File containing the search strings                          Search.txt
SITE_FILE       File containing the sites to query                          Sites.txt

The program operates in 5 main stages. 

1) Setup
    The main function executes and spawn threads, parses the configuration file, and sets up various variables to prepare the following sections to run. It exits on appropriate errors such as invalid files or parameters. 

2) Fetch
    The setup phase added websites to a threadsafe queue protected by a mutex and cond_var. The threads initailized and spun off for the fetching function grab websites from this queue, initializa CURL objects and make requests. It packs the return from this libcurl requests into a website struct and places that into a threadsafe parse queue again protected by a mutex and cond_var.

3) Parse
    The parse threads wake up when an item is placed in the parse_queue. Each parse thread grabs a website object as they are available. It takes a time signature and parse the website for each term in the termlist. It takes counts of these terms and places them into a OUTPUT struct. Each output struct is them placed onto the output_queue. waking the output thread. 

4) Output
    A single thread wakes when the output queue recieves and OUTPUT struct. It parses the struct for the appropriate data and writes to a html file along with other html text. A sigalarm switches these files every time a batch intiates.

5) Cleanup
    A SIGINT or SIGTERM triggers cleanup. This cancels threads and closes all relevent objects, and exits gracefullly. 

A sigalarm function controls when a batch is added to the fetch_queue and controls the output file. 
