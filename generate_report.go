package main

import (
	"bufio"
	"encoding/csv"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"math"
	"net/http"
	"os"
	"strconv"
	"strings"
	"time"
)

func getMaxVal(a map[int64]float64) float64 {
	if len(a) == 0 {
		return 0
	}
	var maxVal = a[0]
	for _, value := range a {
		if maxVal < value {
			maxVal = value
		}
	}
	return maxVal
}

func parseTs(duration string) (startEpoch, endEpoch int64) {
	now := time.Now()
	today00 := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, now.Location())

	switch duration {
	case "yesterday":
		startEpoch = today00.Add(-24 * time.Hour).Unix()
		endEpoch = today00.Unix()

	case "month":
		startEpoch = today00.Add(-time.Hour * 24 * 30).Unix()
		endEpoch = now.Unix()

	case "all":
		startEpoch = 0
		endEpoch = now.Unix()

	case "today":
		fallthrough

	default:
		startEpoch = today00.Unix()
		endEpoch = now.Unix()
	}
	return
}

type record struct {
	Start int64
	End   int64
	Vol   float64
}

func getRequiredRecords(filename string, startEpoch, endEpoch int64) *map[string][]record {
	// start aggregating on entire file
	fptr, err := os.Open(filename)
	if err != nil {
		panic("Failed to open file : " + filename)
	}
	reader := csv.NewReader(bufio.NewReader(fptr))

	// working variables
	var prevTs, curTs, sInputID int64
	var device string
	var netVol float64
	// var portVol, netVol float64

	// currentState
	var curDevice string
	var vol float64
	srcWiseVol := make(map[int64]float64)
	deviceWiseRecords := make(map[string][]record)

	prevTs = -1
	curDevice = ""

	for {
		line, err := reader.Read()
		if err == io.EOF {
			break
		} else if err != nil {
			panic("Failed to parse CSV")
		}

		// parse data
		curTs, _ = strconv.ParseInt(line[0], 10, 64)
		sInputID, _ = strconv.ParseInt(line[1], 10, 64)
		device = line[2]
		// portVol, _ = strconv.ParseFloat(line[3], 10)
		netVol, _ = strconv.ParseFloat(line[4], 10)

		if prevTs == -1 { // the first record
			prevTs = curTs
		}

		if curTs < startEpoch {
			continue
		}
		if curTs > endEpoch {
			break
		}

		vol = getMaxVal(srcWiseVol)

		// filter out zero volume records
		if vol > 0 {
			// detect device change
			// if yes, record volume set to zero on that device
			if device != curDevice {
				deviceWiseRecords[curDevice] = append(deviceWiseRecords[curDevice], record{prevTs, curTs, vol})
				curDevice = device
			} else {
				deviceWiseRecords[curDevice] = append(deviceWiseRecords[curDevice], record{prevTs, curTs, vol})
			}
		}

		if netVol < 0 {
			// remove this src if it has been removed
			delete(srcWiseVol, sInputID)
		} else {
			// update the volume of this source - new or change
			srcWiseVol[sInputID] = netVol
		}

		prevTs = curTs
	}

	delete(deviceWiseRecords, "")

	// for key, records := range deviceWiseRecords {
	// 	fmt.Println(key)
	// 	for _, r := range records {
	// 		fmt.Println(r)
	// 	}
	// 	fmt.Println()
	// }
	return &deviceWiseRecords
}

type summary_t struct {
	Device       string
	AvgVol       float64
	PercTime     int
	TotalTime    int64
	TimeAboveAvg int64
}

func getSummary(deviceWiseRecords *map[string][]record) []summary_t {
	var totalVol, N, avgVol float64
	var totalTime, timeAboveAverage, totalTimeListening int64

	summary := make([]summary_t, 0)
	totalTimeListening = 0

	// for each device
	for deviceName, records := range *deviceWiseRecords {

		// average volume on the device
		// total time spent listening
		totalVol = 0.0
		totalTime = 0
		N = 0.0
		for _, r := range records {
			N++
			totalVol += float64(r.End-r.Start) * r.Vol
			totalTime += r.End - r.Start
		}
		avgVol = totalVol / float64(totalTime)

		// total time spent listening above average volume
		timeAboveAverage = 0
		for _, r := range records {
			if r.Vol > avgVol {
				timeAboveAverage += r.End - r.Start
			}
		}

		summary = append(summary, summary_t{
			Device:       deviceName,
			AvgVol:       avgVol,
			TotalTime:    totalTime,
			TimeAboveAvg: timeAboveAverage,
			PercTime:     0,
		})
		totalTimeListening += totalTime
	}

	for i, _ := range summary {
		summary[i].PercTime = int(math.Round(float64(summary[i].TotalTime) * 100.0 / float64(totalTimeListening)))
	}

	return summary
}

type hourSummary_t struct {
	Hour   int
	AvgVol int
	Device string
}

func getHourWiseSummary(deviceWiseRecords *map[string][]record) []hourSummary_t {

	hourlySummary := make([]hourSummary_t, 24)
	totalTime := make([]int64, 24)
	totalVol := make([]float64, 24)
	headphoneTime := make([]int64, 24)
	speakerTime := make([]int64, 24)
	nSamples := make([]float64, 24)
	var timeToAdd int64

	for i := 0; i < 24; i++ {
		totalTime[i] = 0
		totalVol[i] = 0
		headphoneTime[i] = 0
		speakerTime[i] = 0
		nSamples[i] = 0
	}

	var startTime, endTime, curTime time.Time

	for deviceName, records := range *deviceWiseRecords {
		for _, r := range records {
			// for all hours it belongs to
			startTime = time.Unix(r.Start, 0)
			endTime = time.Unix(r.End, 0)

			for curTime = startTime; curTime.Before(endTime); curTime = curTime.Add(1 * time.Hour) {
				timeToAdd = 0
				if endTime.Before(curTime.Add(1 * time.Hour)) {
					timeToAdd = endTime.Unix() - curTime.Unix()
				} else {
					timeToAdd = 3600
				}

				totalTime[curTime.Hour()] += timeToAdd
				if strings.Contains(deviceName, "speaker") {
					speakerTime[curTime.Hour()] += timeToAdd
				} else if strings.Contains(deviceName, "headphone") {
					headphoneTime[curTime.Hour()] += timeToAdd
				}
				totalVol[curTime.Hour()] += r.Vol
				nSamples[curTime.Hour()] += 1
			}
		}
	}

	// prepare hourly summary
	for i := 0; i < 24; i++ {
		hourlySummary[i].Hour = i
		if totalVol[i] != 0 {
			hourlySummary[i].AvgVol = int(math.Round(totalVol[i] * 100 / nSamples[i]))
		}
		if headphoneTime[i] < speakerTime[i] {
			hourlySummary[i].Device = "Speakers"
		} else {
			hourlySummary[i].Device = "Headphones"
		}
	}

	return hourlySummary
}

func displayHourlySummary(hourlySummary []hourSummary_t) {
	fmt.Println("Hour wise usage :")
	fmt.Println("------------------")
	fmt.Println(" Hour | Average vol | Mostly listen on")
	fmt.Println("------|-------------|------------------")
	for i := 0; i < 24; i++ {
		if hourlySummary[i].AvgVol != 0 {
			fmt.Printf("  %2d  |     %2d %%    | %s \n", hourlySummary[i].Hour, hourlySummary[i].AvgVol, hourlySummary[i].Device)
		}
	}
	fmt.Println("---------------------------------------")
}

// func getPropsForDevice(deviceName string) (commonName string, r, g, b, a float64) {
// 	if strings.Contains(deviceName, "headphones") {
// 		return "Headphones", 255.0, 79.0, 94.0, 0.0 // red
// 	} else if strings.Contains(deviceName, "speaker") {
// 		return "Laptop", 183.0, 212.0, 63.0, 0.0 // green
// 	}
// 	return "unknown", 0.0, 0.0, 0.0, 0.0
// }

// func visualizeSummary(summary []summary_t) {
// 	// i am going to consider only speaker and headphone for my machine
// 	// TODO think of some way to extend it - laptop speaker/headphone/HDMI monitor
// 	var headphone, speaker summary_t
// 	for _, s := range summary {
// 		if strings.Contains(s.Device, "headphones") {
// 			headphone = s
// 		} else if strings.Contains(s.Device, "speaker") {
// 			speaker = s
// 		}
// 	}
// }

func secToHuman(seconds int64) string {
	var h, m, s int64

	h = int64(seconds / 3600)
	m = int64(seconds/60) - 60*h
	s = seconds - h*3600 - m*60

	if h != 0 {
		return fmt.Sprintf("%d hrs %d min", h, m)
	} else if m != 0 {
		return fmt.Sprintf("%d min", m)
	} else {
		return fmt.Sprintf("%d sec", s)
	}
}

func displaySummary(summary []summary_t) {
	for _, s := range summary {
		// print device name
		if strings.Contains(s.Device, "speaker") {
			fmt.Println("Laptop speaker : ")
			fmt.Println("-----------------")
		} else if strings.Contains(s.Device, "headphone") {
			fmt.Println("Headphones : ")
			fmt.Println("-------------")
		} else {
			fmt.Print(s.Device, ":")
		}

		// print stats
		fmt.Println("Used", s.PercTime, "% time.")
		fmt.Println("Average volume :", math.Round(s.AvgVol*100), "%")
		fmt.Println("Total time :", secToHuman(s.TotalTime))
		fmt.Println("Loud for :", secToHuman(s.TimeAboveAvg))
		fmt.Println()
	}
}

func main() {

	var logPath, duration string
	var interactive bool
	var port int

	flag.StringVar(&logPath, "logfile", "", "Path to log file")
	flag.StringVar(&duration, "duration", "today", "Period - today/yesterday/everyday/week/month")
	flag.BoolVar(&interactive, "interactive", false, "Start server to interactively view summary")
	flag.IntVar(&port, "port", 5000, "Port")

	flag.Parse()
	if logPath == "" {
		fmt.Println("Missing path to log file.")
		os.Exit(1)
	}

	if interactive {
		startServer(logPath, port)
	} else {
		var startEpoch, endEpoch int64
		startEpoch, endEpoch = parseTs(duration)

		records := getRequiredRecords(logPath, startEpoch, endEpoch)

		summary := getSummary(records)
		displaySummary(summary)

		hourlySummary := getHourWiseSummary(records)
		displayHourlySummary(hourlySummary)
	}

}

func startServer(logPath string, port int) {
	http.HandleFunc("/summary", func(w http.ResponseWriter, r *http.Request) {
		startEpoch, _ := strconv.ParseInt(r.URL.Query().Get("start"), 10, 64)
		endEpoch, _ := strconv.ParseInt(r.URL.Query().Get("end"), 10, 64)

		fmt.Println("summary for", startEpoch, endEpoch)
		records := getRequiredRecords(logPath, startEpoch, endEpoch)
		summary := getSummary(records)

		response := make([]map[string]string, 0)
		for _, s := range summary {
			response = append(response, map[string]string{
				"device":           s.Device,
				"avgVol":           strconv.Itoa(int(math.Round(s.AvgVol * 100))),
				"percTime":         strconv.Itoa(s.PercTime),
				"totalTime":        strconv.Itoa(int(s.TotalTime)),
				"totalTimeStr":     secToHuman(s.TotalTime),
				"timeAboveAverage": secToHuman(s.TimeAboveAvg),
			})
		}

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(response)
	})

	http.HandleFunc("/hourly", func(w http.ResponseWriter, r *http.Request) {
		startEpoch, _ := strconv.ParseInt(r.URL.Query().Get("start"), 10, 64)
		endEpoch, _ := strconv.ParseInt(r.URL.Query().Get("end"), 10, 64)

		fmt.Println("hourly report for", startEpoch, endEpoch)
		records := getRequiredRecords(logPath, startEpoch, endEpoch)
		hourlySummary := getHourWiseSummary(records)

		w.Header().Set("Content-Type", "application/json")
		json.NewEncoder(w).Encode(hourlySummary)
	})

	fs := http.FileServer(http.Dir("docs"))
	http.Handle("/", fs)

	fmt.Printf("Starting server. Go to http://localhost:%d/\n", port)
	http.ListenAndServe(":"+strconv.Itoa(port), nil)
}
