const wifiStatus = document.getElementById('wifi-status')
const ssidStatus = document.getElementById('ssid-status')
const macAddress = document.getElementById('mac-address')
const ipAddress = document.getElementById('ip-address')
const gateway = document.getElementById('gateway')
const subnet = document.getElementById('subnet')
const serverPort = document.getElementById('server-port')
const callStatus = document.getElementById('call-status')
const baudStatus = document.getElementById('baud-status')

const baud = document.getElementById('baud')
const flow = document.getElementById('flow')
const echo = document.getElementById('echo')
const quiet = document.getElementById('quiet')
const verbose = document.getElementById('verbose')
const telnet = document.getElementById('telnet')
const polarity = document.getElementById('polarity')
const autoanswer = document.getElementById('autoanswer')
const ssid = document.getElementById('ssid')
const password = document.getElementById('password')

fetch(`/get-settings`)
  .then((response) => {
        response.json()
        // console.log(response)
        return response
    })
  .then((data) => {
    console.log(data)
    wifiStatus.innerText = data.wifiStatus
  });