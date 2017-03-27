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

void removeBetweenStrings(const std::string &s, const std::string startRemove, const std::string endRemove);
vector<string> split(const string &s, char delim);
void configure(string config);
void handle_exit(int sig);
void handle_alarm(int sig);
size_t write_data(char *ptr, size_t size, size_t nmemb, void *userdata);
void * parse_website(void * v);
void * fetch_website(void * v);
void * write_output(void * v);
void create_html_header(int);

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

/**
 * Removes all characters from a string falling between two substrings.
 * @param s The string to remove characters from
 * @param startRemove the substring to start removing characters at
 * @param endRemove the substring to end removing characters at
 */
void removeBetweenStrings(std::string &s, const std::string startRemove, const std::string endRemove) {
  size_t startPos = s.find(startRemove, 0);
  size_t endPos = s.find(endRemove, 0);
  while( startPos != std::string::npos && endPos != std::string::npos ) {
    if (endPos > startPos) {
      s.erase(startPos, endPos - startPos + 1);
      endPos = s.find(endRemove, endPos);
      startPos = s.find(startRemove, startPos);
    } else {
      endPos = s.find(endRemove, endPos);
    }
  }
}

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
            PERIOD_FETCH = stoi(params[1]);
        }
        else if(!params[0].compare("NUM_FETCH")){
            NUM_FETCH = stoi(params[1]);
        }
        else if(!params[0].compare("NUM_PARSE")){
            NUM_PARSE = stoi(params[1]);
        }
        else if(!params[0].compare("SEARCH_FILE")){
            SEARCH_FILE = params[1];
        }
        else if(!params[0].compare("SITE_FILE")){
            SITE_FILE = params[1];
        }
        else{
            cout << "Unknown parameter, ignoring" << endl;
        }
    }
}

void handle_exit(int sig) {
    cout << "Cleaning up" << endl;
    fetch_looping = false;
    parse_looping = false;
    parse_cv.notify_all();
    fetch_cv.notify_all();
    
    for(int i = 0; i < NUM_FETCH; i++){
        int rc = pthread_join(fetch_threads[i], NULL);
        assert(rc == 0);
    }
    for(int i = 0; i < NUM_PARSE; i++){
        int rc = pthread_join(parse_threads[i], NULL);
        assert(rc == 0);
    }
    
    int rc = pthread_join(output_thread, NULL);
    assert(rc == 0);
    
    curl_global_cleanup();

    do_task = false;
    f_outfile << "    for(var i=0; i < data.length; i++) {" << endl
    << "      var obj = data[i];" << endl
    << "      var r = parseInt(Math.random() * 255);" << endl
    << "      var g = parseInt(Math.random() * 255);" << endl
    << "      var b = parseInt(Math.random() * 255);" << endl
    << "      var light_color = 'rgba(' + r + ', ' + g + ', ' + b + ', 0.2)';" << endl
    << "      var color = 'rgba(' + r + ', ' + g + ', ' + b + ', 1.0)';" << endl
    << "      labels.push(obj.term);" << endl
    << "      if (dataDict[obj.site] === undefined) {" << endl
    << "        dataDict[obj.site] = {" << endl
    << "          label: obj.site," << endl
    << "          data: []," << endl
    << "          fillColor: light_color," << endl
    << "          strokeColor: color," << endl
    << "          pointColor: color," << endl
    << "          pointStrokeColor: '#fff'," << endl
    << "          pointHighlightFill: '#fff'," << endl
    << "          pointHighlightStroke: color" << endl
    << "        }" << endl
    << "      }" << endl
    << "      dataDict[obj.site].data.push(obj.count);" << endl
    << "    }" << endl
    << "    labels = Array.from(new Set(labels));" << endl
    << "    for (var key in dataDict) {" << endl
    << "      datasets.push(dataDict[key]);" << endl
    << "    }" << endl
    << "    var data = {" << endl
    << "      labels: labels," << endl
    << "      datasets: datasets," << endl
    << "    };" << endl
    << "    $(function() {" << endl
    << "      var ctx = document.getElementById('myChart').getContext('2d');" << endl
    << "      var myRadarChart = new Chart(ctx).Radar(data,option); " << endl
    << "      document.getElementById('legendDiv').innerHTML = myRadarChart.generateLegend();" << endl
    << "    });" << endl
    << "  </script>" << endl
    << "</body>" << endl
    << "</html>" << endl
    ;
    f_outfile.close();
    exit(1);
}

void handle_alarm(int sig){
    file_num++;
    
    f_outfile << "    for(var i=0; i < data.length; i++) {" << endl
              << "      var obj = data[i];" << endl
              << "      var r = parseInt(Math.random() * 255);" << endl
              << "      var g = parseInt(Math.random() * 255);" << endl
              << "      var b = parseInt(Math.random() * 255);" << endl
              << "      var light_color = 'rgba(' + r + ', ' + g + ', ' + b + ', 0.2)';" << endl
              << "      var color = 'rgba(' + r + ', ' + g + ', ' + b + ', 1.0)';" << endl
              << "      labels.push(obj.term);" << endl
              << "      if (dataDict[obj.site] === undefined) {" << endl
              << "        dataDict[obj.site] = {" << endl
              << "          label: obj.site," << endl
              << "          data: []," << endl
              << "          fillColor: light_color," << endl
              << "          strokeColor: color," << endl
              << "          pointColor: color," << endl
              << "          pointStrokeColor: '#fff'," << endl
              << "          pointHighlightFill: '#fff'," << endl
              << "          pointHighlightStroke: color" << endl
              << "        }" << endl
              << "      }" << endl
              << "      dataDict[obj.site].data.push(obj.count);" << endl
              << "    }" << endl
              << "    labels = Array.from(new Set(labels));" << endl
              << "    for (var key in dataDict) {" << endl
              << "      datasets.push(dataDict[key]);" << endl
              << "    }" << endl
              << "    var data = {" << endl
              << "      labels: labels," << endl
              << "      datasets: datasets," << endl
              << "    };" << endl
              << "    $(function() {" << endl
              << "      var ctx = document.getElementById('myChart').getContext('2d');" << endl
              << "      var myRadarChart = new Chart(ctx).Radar(data,option); " << endl
              << "      document.getElementById('legendDiv').innerHTML = myRadarChart.generateLegend();" << endl
              << "    });" << endl
              << "  </script>" << endl
              << "</body>" << endl
              << "</html>" << endl
              ;

    //close previous, make new
    f_outfile.close();
    create_html_header(file_num);
    
    cout << "refill queue" << endl;
    unique_lock<mutex> fetch_guard(fetch_m);
    for(int i = 0; i < (int)sites.size(); i++){
        fetch_queue.push(sites[i]);
    }
    fetch_guard.unlock();
    fetch_cv.notify_all();
    
    alarm(PERIOD_FETCH);
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
        int counts[searches.size()];
        
        for(int i =  0; i < (int)searches.size(); i++){
            
            int count = 0;

            // Remove all html comments
            removeBetweenStrings(info.content, "<!--", "-->");
            // Remove head tag
            removeBetweenStrings(info.content, "<head>", "</head>");
            // Remove inline javascript
            removeBetweenStrings(info.content, "<script>", "</script>");
            // Remove all remaining tags
            removeBetweenStrings(info.content, "<", ">");

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
        long http_code = 0;

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream);

        
        curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_easy_cleanup(curl);
        
        string output = stream.str();
        
        if( http_code >= 300 ){
            cout << "Error or timeout on retrieving website." << endl;
            cout << "Website: " << url << ", Status code: " << http_code << endl;
        }
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
        
        f_outfile << "    data.push({" << endl
                  << "      time: '" << info.time << "'," << endl
                  << "      term: '" << info.term << "'," << endl
                  << "      site: '" << info.site << "'," << endl
                  << "      count: " << info.count << "," << endl
                  << "    });" << endl
                  ;
    }
    
    return 0;
}

void create_html_header(int file_num) {
    cout << "Creating file: " << file_num << endl;
    f_outfile.open(to_string(file_num) + ".html");
    if(!f_outfile){
        cout << "could not open file to write output, exiting" << endl;
        exit(1);
    }

    f_outfile << "<!DOCTYPE html>" << endl
              << "<html>" << endl
              << "<head>" << endl
              << "  <title>Keyword Krawler</title>" << endl
              << "  <link rel=\"stylesheet\" href=\"https://cdnjs.cloudflare.com/ajax/libs/mdbootstrap/4.3.0/css/mdb.min.css\">" << endl
              << "  <script src=\"https://cdnjs.cloudflare.com/ajax/libs/jquery/2.2.1/jquery.min.js\"></script>" << endl
              << "  <script src=\"https://cdnjs.cloudflare.com/ajax/libs/mdbootstrap/4.3.0/js/mdb.min.js\"></script>" << endl
              << "  <style>" << endl
              << "    ul.radar-legend {" << endl
              << "      text-align: center;" << endl
              << "      margin-top: 2rem;" << endl
              << "    }" << endl
              << "    ul.radar-legend li {" << endl
              << "      display: inline-block;" << endl
              << "      margin-right: 10px;" << endl
              << "    }" << endl
              << "    ul.radar-legend li span{" << endl
              << "      padding: 5px 10px;" << endl
              << "      color: #fff;" << endl
              << "    }" << endl
              << "  </style>" << endl
              << "</head>" << endl
              << "<body>" << endl
              << "  <div id='legendDiv'></div>" << endl
              << "  <canvas id='myChart'></canvas>" << endl
              << "  <script>" << endl
              << "    var data = [];" << endl
              << "    var labels = [];" << endl
              << "    var dataDict = [];" << endl
              << "    var datasets = [];" << endl
              << "    var option = {responsive: true};" << endl
              ;
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
    
    // get contents of sites file
    
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
    
    // get contents of search file
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
    // end of search file
    
    
    // create threads for both fetch and parse
    fetch_threads = (pthread_t*)malloc(NUM_FETCH * sizeof(pthread_t));
    parse_threads = (pthread_t*)malloc(NUM_PARSE * sizeof(pthread_t));
    
    // two arrays with the resource information
    // first round start

    create_html_header(0);
    
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
    alarm( PERIOD_FETCH );
    while(do_task){
        
    }
    return 0;
}
