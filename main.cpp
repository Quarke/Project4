//
//  main.cpp
//  Project4
//
//  Created by Thomas on 3/21/17.
//  Copyright © 2017 Tommy Flanagan. All rights reserved.
//

#include <iostream>
#include <stdio.h>
#include <fstream>
#include <string>
#include <queue>
#include <signal.h>
#include <condition_variable>
#include <mutex>
#include <sstream>
#include <ctime>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <curl/curl.h>

using namespace std;

//function declarations
template<typename Out>
void split(const std::string &s, char delim, Out result);
vector<string> split(const string &s, char delim);
void configure(string config);
void handle_exit(int sig);
void handle_alarm(int sig);
size_t write_data(char *ptr, size_t size, size_t nmemb, void *userdata);
void * parse_website(void * v);
void * fetch_website(void * v);
void * write_output(void * v);

struct WEBSITE {
    string content;
    string url;
};

struct OUTPUT {
    string time;
    string term;
    string site;
    int count;
};

//default variables
int file_num = 0;
int PERIOD_FETCH = 180;
int NUM_FETCH = 5;
int NUM_PARSE = 5;
string SEARCH_FILE = "Search.txt";
string SITE_FILE = "Sites.txt";
ofstream f_outfile;

bool do_task = true;
bool fetch_looping = true;
bool parse_looping = true;
vector<string> searches;
vector<string> sites;

pthread_t* fetch_threads;
pthread_t* parse_threads;

pthread_t output_thread;

queue<string> fetch_queue;
mutex fetch_m;
condition_variable fetch_cv;

queue<WEBSITE> parse_queue;
mutex parse_m;
condition_variable parse_cv;

queue<OUTPUT> output_queue;
mutex output_m;
condition_variable output_cv;

template<typename Out>
void split(const std::string &s, char delim, Out result) {
    std::stringstream ss;
    ss.str(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        *(result++) = item;
    }
}

vector<string> split(const string &s, char delim) {
    vector<std::string> elems;
    split(s, delim, back_inserter(elems));
    return elems;
}

void configure(string config){
    ifstream f_config(config);
    
    if(!f_config){
        cout << "Error opening config file" << endl;
        exit(1);
    }
    string line;
    while(!f_config.eof())
    {
        getline(f_config, line);
        if(line.empty()){
            break;
        }
        vector<string> params = split(line, '=');
        cout << params[0] << " : "  << params[1] << endl;

        if(!params[0].compare("PERIOD_FETCH")){
            cout << params[0] << " -> "  << params[1] << endl;
            PERIOD_FETCH = stoi(params[1]);
        }
        else if(!params[0].compare("NUM_FETCH")){
            cout << params[0] << " -> " << params[1] << endl;
            NUM_FETCH = stoi(params[1]);
        }
        else if(!params[0].compare("NUM_PARSE")){
            cout << params[0] << " -> " << params[1] << endl;
            NUM_PARSE = stoi(params[1]);
        }
        else if(!params[0].compare("SEARCH_FILE")){
            cout << params[0] << " -> " << params[1] << endl;
            SEARCH_FILE = params[1];
        }
        else if(!params[0].compare("SITE_FILE")){
            cout << params[0] << " -> " << params[1] << endl;
            SITE_FILE = params[1];
        }
        else{
            cout << "Unknown parameter, ignoring" << endl;
        }
    }
}

void handle_exit(int sig) {
    fetch_looping = false;
    parse_looping = false;
    
    for(int i = 0; i < NUM_FETCH; i++){
        int rc = pthread_cancel(fetch_threads[i]);
        assert(rc == 0);
    }
    for(int i = 0; i < NUM_PARSE; i++){
        int rc = pthread_cancel(parse_threads[i]);
        assert(rc == 0);
    }
    
    int rc = pthread_cancel(output_thread);
    assert(rc == 0);
    
    curl_global_cleanup();

    do_task = false;
    f_outfile.close();
    exit(1);
}

void handle_alarm(int sig){
    file_num++;
    
    
    //the messiest html writing you will ever see
    f_outfile << "</tbody></table>";
    f_outfile << "</body></html>";
    f_outfile.close();
    
    //close previous, make new
    
    f_outfile.open(to_string(file_num) + ".html");
    f_outfile << "<!DOCTYPE html><html><head><title>Page Title</title>";
    
    f_outfile << "<script type=\"text/javascript\" charset=\"utf8\" src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.2.1/jquery.min.js\"></script>";
    
    f_outfile << "<link rel=\"stylesheet\" type=\"text/css\" href=\"https://cdn.datatables.net/1.10.13/css/jquery.dataTables.min.css\">";
    f_outfile << "<script type=\"text/javascript\" charset=\"utf8\" src=\"https://cdn.datatables.net/1.10.13/js/jquery.dataTables.min.js\"></script>";
    
    f_outfile << "<script>$(document).ready(function(){$('#example').DataTable();});</script>";
    f_outfile << "</head><body><table id=\"example\" class=\"display\" cellspacing=\"0\" width=\"100%\"><thead><tr>";
    f_outfile << "<th>Time</th><th>Phrase</th><th>Site</th><th>Count</th></tr></thead>";
    
    if(!f_outfile){
        cout << "could not open file to write output, exiting" << endl;
        exit(1);
    }
    cout << "refill queue" << endl;
    unique_lock<mutex> fetch_guard(fetch_m);
    for(int i = 0; i < (int)sites.size(); i++){
        fetch_queue.push(sites[i]);
    }
    fetch_guard.unlock();
    fetch_cv.notify_all();
    
    alarm(10);
}

size_t write_data(char *ptr, size_t size, size_t nmemb, void *userdata) {
    std::ostringstream *stream = (std::ostringstream*)userdata;
    size_t count = size * nmemb;
    stream->write(ptr, count);
    return count;
}

void * parse_website(void * v){
    while(parse_looping){
        unique_lock<mutex> guard(parse_m);
        while(parse_queue.empty()){
            parse_cv.wait(guard);
        }
        
        WEBSITE info = parse_queue.front();
        parse_queue.pop();
        guard.unlock();
        parse_cv.notify_all();

        time_t rawtime;
        struct tm * timeinfo;
        char time_buffer[80];
        
        time (&rawtime);
        timeinfo = localtime(&rawtime);
        
        strftime(time_buffer, sizeof(time_buffer),"%d-%m-%Y %I:%M:%S",timeinfo);
        string time_str(time_buffer);
        int counts[searches.size() ];
        
        for(int i =  0; i < (int)searches.size(); i++){
            
            int count = 0;
            size_t nPos = info.content.find(searches[i], 0); // fist occurrence
            while(nPos != string::npos)
            {
                count++;
                nPos = info.content.find(searches[i], nPos+1);
            }
            counts[i] = count;
            OUTPUT o;
            o.time = time_str;
            o.term = searches[i];
            o.site = info.url;
            o.count = counts[i];
            
            
            unique_lock<mutex> output_guard(output_m);
            output_queue.push(o);
            output_guard.unlock();
            output_cv.notify_all();
            //printf("%s, %s, %s, %d\n", time_str.c_str(), searches[i].c_str(), info.url.c_str(), counts[i]);
        }
    }
    return 0;
}

void * fetch_website(void * v){
    while(fetch_looping){
        unique_lock<mutex> guard(fetch_m);
        while(fetch_queue.empty()){
            fetch_cv.wait(guard);
        }
        
        string url = fetch_queue.front();
        fetch_queue.pop();
        guard.unlock();
        fetch_cv.notify_all();
        
        ostringstream stream;
        
        CURL * curl;
        curl = curl_easy_init();
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);
        
        curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        string output = stream.str();
        
        WEBSITE info;
        info.content = output;
        info.url = url;
        
        unique_lock<mutex> parse_guard(parse_m);
        parse_queue.push(info);
        parse_guard.unlock();
        parse_cv.notify_all();
    }
    
    return 0;
}

void * write_output(void * v){
    cout << "Writing output ready" << endl;
    while(parse_looping) {
        unique_lock<mutex> guard(output_m);
        while(output_queue.empty()){
            output_cv.wait(guard);
        }
        OUTPUT info = output_queue.front();
        output_queue.pop();
        
        guard.unlock();
        output_cv.notify_all();
        
        f_outfile << "<tr>";
        
        f_outfile << "<td>" << info.time << "</td>";
        f_outfile << "<td>" << info.term << "</td>";
        f_outfile << "<td>" << info.site << "</td>";
        f_outfile << "<td>" << info.count<< "</td>";
        
        f_outfile << "</tr>";
    }
    
    return 0;
}

int main(int argc, const char * argv[]) {
    
    if (argc < 2){
        cout << "No configfile file provided, exiting" << endl;
        exit(1);
    }
    else {
        configure(argv[1]);
    }
    
    if(NUM_PARSE == 0 || NUM_FETCH == 0){
        cout << "Threads set to 0, terminating" << endl;
    }
    
    //set signal handlers
    signal(SIGINT, handle_exit);
    
    
    //setup curl
    curl_global_init(CURL_GLOBAL_ALL);
    
    /* get contents of sites file */
    
    ifstream f_sites(SITE_FILE);

    if(!f_sites){
        cout<<"Error opening site file"<<endl;
        return -1;
    }
    string line;
    while(!f_sites.eof())
    {
        getline(f_sites, line);
        if(!line.empty())
        {
            sites.push_back(line);
        }
    }
    /* end of sites file */
    
    /* get contents of search file */
    ifstream f_search(SEARCH_FILE);
    
    if(!f_search){
        cout<<"Error opening search file"<<endl;
        return -1;
    }
    while(!f_search.eof())
    {
        getline(f_search, line);
        if(!line.empty())
        {
            searches.push_back(line);
        }
    }
    /* end of search file */
    
    
    /* create threads for both fetch and parse*/
    fetch_threads = (pthread_t*)malloc(NUM_FETCH * sizeof(pthread_t));
    parse_threads = (pthread_t*)malloc(NUM_PARSE * sizeof(pthread_t));
    
    // two arrays with the resource information
    // first round start
    
    
    f_outfile.open("0.html");
    if(!f_outfile){
        cout << "could not open file to write output, exiting" << endl;
        exit(1);
    }
    f_outfile << "<!DOCTYPE html><html><head><title>Page Title</title>";
    
    f_outfile << "<script type=\"text/javascript\" charset=\"utf8\" src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/3.2.1/jquery.min.js\"></script>";

    f_outfile << "<link rel=\"stylesheet\" type=\"text/css\" href=\"https://cdn.datatables.net/1.10.13/css/jquery.dataTables.min.css\">";
    f_outfile << "<script type=\"text/javascript\" charset=\"utf8\" src=\"https://cdn.datatables.net/1.10.13/js/jquery.dataTables.min.js\"></script>";
    
    f_outfile << "<script>$(document).ready(function(){$('#example').DataTable();});</script>";
    
    f_outfile << "</head><body><table id=\"example\" class=\"display\" cellspacing=\"0\" width=\"100%\"><thead><tr>";

    f_outfile << "<th>Time</th><th>Phrase</th><th>Site</th><th>Count</th></tr></thead>";
    
    for(int i = 0; i < (int)sites.size(); i++){
        //fetch_website(sites[i], search, search_size);
        fetch_queue.push(sites[i]);
    }
    
    for (int i = 0; i < NUM_FETCH; i++) {
        //spin off thread
        int rc = pthread_create(&fetch_threads[i], NULL, fetch_website, NULL);
        
        //check if it was successful
        assert(rc == 0);
    }
    
    for (int i = 0; i < NUM_FETCH; i++) {
        //spin off thread
        int rc = pthread_create(&parse_threads[i], NULL, parse_website, NULL);
    
        //check if it was successful
        assert(rc == 0);
    }
    
    output_thread = *(pthread_t*)malloc(1 * sizeof(pthread_t));
    
    int rc = pthread_create(&output_thread, NULL, write_output, NULL);
    assert(rc == 0);
    
    signal( SIGALRM, handle_alarm);
    alarm( 10 );
    while(do_task){
        
    }
    return 0;
}
