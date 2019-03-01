let btns = document.getElementsByClassName('btn')
let avgVol = document.getElementById('averageVol')
let totalTime = document.getElementById('totalTime')
let loud = document.getElementById('loud')

function epochNow() {
  return Math.round(Date.now() / 1000)
}

for (let btn of btns) {
  btn.onclick = (e) => {
    console.log('clicked on', e.target.id)
    e.target.className = "btn active"
    for (let other of btns) {
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
      start = epochNow() - 7 * 24 * 360
      end = epochNow()
      break

    case 'all-time':
      start = 0
      end = epochNow()
      break
    }

    // perform xhr
    fetch('api?start=' + start + '&end=' + end)
      .then(res => {
        return res.json()
      })
      .then(json => {
        for (let d of json) {
          if (d.device.includes("speaker")) {
            avgVol.getElementsByClassName('left')[0].innerText = d.avgVol + '%'
            totalTime.getElementsByClassName('left')[0].innerText = d.totalTime
            loud.getElementsByClassName('left')[0].innerText = d.timeAboveAverage
          } else if (d.device.includes('headphone')) {
            avgVol.getElementsByClassName('right')[0].innerText = d.avgVol + '%'
            totalTime.getElementsByClassName('right')[0].innerText = d.totalTime
            loud.getElementsByClassName('right')[0].innerText = d.timeAboveAverage
          }
        }
      })
  }
}


document.getElementById('today').click()
