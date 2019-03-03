let intervals = document.getElementById('intervals').getElementsByClassName('btn'),
    tabs = document.getElementById('nav').getElementsByClassName('btn')

let summaryTab = document.getElementById('summary'),
    hourlyTab = document.getElementById('hourly-summary')

let activeTab = "summary",
    activeInterval = "today"

let dummyResponse = {}

let canvas = document.getElementById('hourly-summary-canvas'),
    ctx = canvas.getContext('2d'),
    chart = new Chart(ctx, {type: 'bar', data: {}, options: {
      scales: {
        xAxes: [{display: true, scaleLabel: {display: true, labelString: 'Hour of day', fontColor: 'black', fontStyle: 'bold'}}],
        yAxes: [{display: true, scaleLabel: {display: true, labelString: "Volume (percent)", fontColor: 'black', fontStyle: 'bold'}}]
      }
    }})


function epochNow() {
  return Math.round(Date.now() / 1000)
}

function initInteravlBtns() {
  for (let btn of intervals) {
    btn.onclick = (e) => {
      console.log('clicked on', e.target.id)
      activeInterval = e.target.id
      e.target.className = "btn active"
      for (let other of intervals) {
        if (other.id !== e.target.id)
          other.className = "btn"
      }

      let start = 0, end = 0
      switch(e.target.id) {
      case 'this-month':
        start = epochNow() - 30 * 24 * 360
        end = epochNow()
        break

      case 'yesterday':
        start = epochNow() - 48 * 3600
        end = epochNow() - 24 * 3600
        break

      case 'today':
        start = epochNow() - 24 * 3600
        end = epochNow()
        break

      case 'this-week':
        start = epochNow() - 7 * 24 * 3600
        end = epochNow()
        break

      case 'all-time':
        start = 0
        end = epochNow()
        break
      }

      if (window.location.href.includes('himanshub16.github.io')) {
        updateUi(dummyResponse[activeTab][activeInterval])
      } else {
        // perform xhr
        console.log('performing xhr')
        fetch('/' + activeTab + '?start=' + start + '&end=' + end)
          .then(res => res.json())
          .then(updateUi)
      }
    }
  }
}

function initTabBtns() {
  for (let btn of tabs) {
    btn.onclick = (e) => {
      e.target.className = "btn active"
      for (let other of tabs) {
        if (other.id != e.target.id)
          other.className = "btn"
      }
      activeTab = e.target.id.replace("-btn", "")
      console.log('active tab set to', activeTab)

      if (activeTab == "summary") {
        summaryTab.style.display = "block"
        hourlyTab.style.display = "none"
      } else {
        summaryTab.style.display = "none"
        hourlyTab.style.display = "block"
      }
      document.getElementById(activeInterval).click()
    }
  }
}

function initUi() {
  initInteravlBtns()
  initTabBtns()
}

function updateSummary(summary) {
  let headphoneBar = document.getElementById('headphoneBar')
  let speakerBar = document.getElementById('speakerBar')
  let avgVol = document.getElementById('averageVol')
  let totalTime = document.getElementById('totalTime')
  let loud = document.getElementById('loud')

  for (let s of summary) {
    if (s.device.includes("speaker")) {
      speakerBar.style.flexGrow = s.totalTime
      speakerBar.innerText = s.percTime + '%'
      avgVol.getElementsByClassName('left')[0].innerText = s.avgVol + '%'
      totalTime.getElementsByClassName('left')[0].innerText = s.totalTimeStr
      loud.getElementsByClassName('left')[0].innerText = s.timeAboveAverage
    } else if (s.device.includes('headphone')) {
      headphoneBar.style.flexGrow = s.totalTime
      headphoneBar.innerText = s.percTime + '%'
      avgVol.getElementsByClassName('right')[0].innerText = s.avgVol + '%'
      totalTime.getElementsByClassName('right')[0].innerText = s.totalTimeStr
      loud.getElementsByClassName('right')[0].innerText = s.timeAboveAverage
    }
  }
}

function updateUi(res) {
  if (activeTab == "summary")
    updateSummary(res);
  else if (activeTab == "hourly")
    updateHourlyPlot(res);
  else
    console.error('unknown active team')
}

function updateHourlyPlot(hourlySummary) {
  chart.data = {
    labels: hourlySummary.map(each => each.Hour),
    datasets: [
      {
        label: "Speakers",
        data: hourlySummary.map(each => (each.Device == "Speakers" ? each.AvgVol : 0)),
        backgroundColor: "#59cbb7",
        borderColor: "#59cbb7",
        borderWidth: 1
      },
      {
        label: "Headphones",
        data: hourlySummary.map(each => (each.Device == "Headphones" ? each.AvgVol : 0)),
        backgroundColor: '#ff4f5e',
        borderColor: '#ff4f5e',
        borderWidth: 1
      }
    ]
  }

  chart.update()
}

window.onload = () => {
  fetch('dummy-data.json')
    .then(res => res.json())
    .then(json => {
      dummyResponse = json
      initUi()
      document.getElementById(activeTab + '-btn').click()
    })
}
