/* eslint-disable max-classes-per-file */
/* eslint-disable no-restricted-globals */
/* eslint-disable no-undef */
$(document).ready(() => {
  // if deployed to a site supporting SSL, use wss://
  const protocol = document.location.protocol.startsWith('https') ? 'wss://' : 'ws://';
  const webSocket = new WebSocket(protocol + location.host);

  // A class for holding the last N points of telemetry for a device
  class DeviceData {
    constructor(deviceId) {
      this.deviceId = deviceId;
      this.maxLen = 50;
      this.timeData = new Array(this.maxLen);
      this.temperatureData = new Array(this.maxLen);
      this.humidityData = new Array(this.maxLen);
      this.moistureData = new Array(this.maxLen);
      this.pumpData = new Array(this.maxLen);
    }

    addData(time, temperature, humidity, moisture,pumpStatus) {
      this.timeData.push((new Date(time)).toLocaleString());
      this.temperatureData.push(temperature);
      this.humidityData.push(humidity || null);
      this.moistureData.push(moisture || null);
      this.pumpData.push(pumpStatus);
      if (this.timeData.length > this.maxLen) {
        this.timeData.shift();
        this.temperatureData.shift();
        this.humidityData.shift();
        this.moistureData.shift();
        this.pumpData.shift();
      }
    }
  }

  // All the devices in the list (those that have been sending telemetry)
  class TrackedDevices {
    constructor() {
      this.devices = [];
    }

    // Find a device based on its Id
    findDevice(deviceId) {
      for (let i = 0; i < this.devices.length; ++i) {
        if (this.devices[i].deviceId === deviceId) {
          return this.devices[i];
        }
      }

      return undefined;
    }

    getDevicesCount() {
      return this.devices.length;
    }
  }
 const pumpStatusMap = new Map([[
  0,'Stopped'
 ],[1,'Running']])
  const trackedDevices = new TrackedDevices();

  // Define the chart axes
  const chartData = {
    datasets: [
      {
        fill: false,
        yAxisId: 'yAxis',
        label: 'Temperature(°C)',
        borderColor: 'rgba(255, 204, 0, 1)',
        pointBoarderColor: 'rgba(255, 204, 0, 1)',
        backgroundColor: 'rgba(255, 204, 0, 0.4)',
        pointHoverBackgroundColor: 'rgba(255, 204, 0, 1)',
        pointHoverBorderColor: 'rgba(255, 204, 0, 1)',
        display: 'left'
      },
      {
        fill: false,
        yAxisId: 'yAxis',
        label: 'Humidity(%)',
        borderColor: 'rgba(24, 120, 240, 1)',
        pointBoarderColor: 'rgba(24, 120, 240, 1)',
        backgroundColor: 'rgba(24, 120, 240, 0.4)',
        pointHoverBackgroundColor: 'rgba(24, 120, 240, 1)',
        pointHoverBorderColor: 'rgba(24, 120, 240, 1)',
        spanGaps: true,
      },
      {
        fill: false,
        yAxisId: 'yAxis',
        label: 'Soil moisture(%)',
        borderColor: 'rgba(11, 156, 49, 1)',
        pointBoarderColor: 'rgba(11, 156, 49, 1)',
        backgroundColor: 'rgba(11, 156, 49, 0.6)',
        pointHoverBackgroundColor: 'rgba(11, 156, 49, 1)',
        pointHoverBorderColor: 'rgba(11, 156, 49, 1)',
        spanGaps: true,
      }
    ]
  };
 

  // Get the context of the canvas element we want to select
  const ctx = document.getElementById('iotChart').getContext('2d');
  const myLineChart = new Chart(
    ctx,
    {
      type: 'line',
      data: chartData,
      options: {
     
        responsive: true,
        plugins: {
          legend: {
            position: 'top',
          },
          title: {
            display: true,
            text: 'Chart.js Line Chart'
          }
        },scales: {
          xAxes: [{
              afterTickToLabelConversion: function(data){


                  var xLabels = data.ticks;

                  xLabels.forEach(function (labels, i) {
                          xLabels[i] = '';
                  });
              } 
          }]   
      }
      }
    });
// Define the chart axes
const pumpChartData = {
  datasets: [
    {
      fill: false,
      yAxisId: 'yAxis',
      label: 'Pump status',
      borderColor: 'rgba(255, 204, 0, 1)',
      pointBoarderColor: 'rgba(255, 204, 0, 1)',
      backgroundColor: 'rgba(255, 204, 0, 0.4)',
      pointHoverBackgroundColor: 'rgba(255, 204, 0, 1)',
      pointHoverBorderColor: 'rgba(255, 204, 0, 1)',
      display: 'left'
    }
  ]
};

    const pumpctx = document.getElementById('pumpChart').getContext('2d');
    const pumpLineChart = new Chart(
      pumpctx,
      {
        type: 'line',
        data: pumpChartData,
        options: {
       
          responsive: true,
          plugins: {
            legend: {
              position: 'top',
            },
            title: {
              display: true,
              text: 'Chart.js Line Chart'
            }
          }
        }
      });
  


  // Manage a list of devices in the UI, and update which device data the chart is showing
  // based on selection
  let needsAutoSelect = true;
  const deviceCount = document.getElementById('deviceCount');
  const listOfDevices = document.getElementById('listOfDevices');
  const threshold = document.getElementById('threshold');
  const currTemp = document.getElementById('current_Temp');
  const currHumidity = document.getElementById('current_Humidity');
  const currMoisture = document.getElementById('current_Moisture');
  const pumpStatus = document.getElementById('pump_status');
  function OnSelectionChange() {
    const device = trackedDevices.findDevice(listOfDevices[listOfDevices.selectedIndex].text);
    chartData.labels = device.timeData;
    pumpChartData.label = device.timeData;
    chartData.datasets[0].data = device.temperatureData;
    chartData.datasets[1].data = device.humidityData;
    chartData.datasets[2].data = device.moistureData;
    pumpChartData.datasets[0].data = device.pumpData;
    myLineChart.update();
    pumpLineChart.update();
  }
  listOfDevices.addEventListener('change', OnSelectionChange, false);

  // When a web socket message arrives:
  // 1. Unpack it
  // 2. Validate it has date/time and temperature
  // 3. Find or create a cached device to hold the telemetry data
  // 4. Append the telemetry data
  // 5. Update the chart UI
  webSocket.onmessage = function onMessage(message) {
    try {
      const messageData = JSON.parse(message.data);
      console.log(messageData);

      // time and either temperature or humidity are required
      if (!messageData.MessageDate || (!messageData.IotData.temp && !messageData.IotData.humidity)) {
        return;
      }

      // find or add device to list of tracked devices
      const existingDeviceData = trackedDevices.findDevice(messageData.DeviceId);

      if (existingDeviceData) {
        currHumidity.innerText = messageData.IotData.humidity;
        currMoisture.innerText = messageData.IotData.pot1.moisture;
        currTemp.innerText = messageData.IotData.temp;
        pumpStatus.innerText = pumpStatusMap.get(messageData.IotData.pump);
        threshold.options[threshold.selectedIndex].innerText = messageData.IotData.pot1.threshold;
        threshold.options[threshold.selectedIndex].value = messageData.IotData.pot1.threshold;
        existingDeviceData.addData(messageData.MessageDate.toLocaleString(), messageData.IotData.temp, messageData.IotData.humidity, messageData.IotData.pot1.moisture,pumpStatusMap.get(messageData.IotData.pump));
      } else {
        const newDeviceData = new DeviceData(messageData.DeviceId);
        trackedDevices.devices.push(newDeviceData);
        const numDevices = trackedDevices.getDevicesCount();
        deviceCount.innerText = numDevices === 1 ? `${numDevices} device` : `${numDevices} devices`;
        currHumidity.innerText = messageData.IotData.humidity;
        currMoisture.innerText = messageData.IotData.pot1.moisture;
        currTemp.innerText = messageData.IotData.temp;
        pumpStatus.innerText = pumpStatusMap.get(messageData.IotData.pump);
        threshold.options[threshold.selectedIndex].innerText = messageData.IotData.pot1.threshold;
        threshold.options[threshold.selectedIndex].value = messageData.IotData.pot1.threshold;
        newDeviceData.addData(messageData.MessageDate.toLocaleString(), messageData.IotData.temp, messageData.IotData.humidity, messageData.IotData.pot1.moisture,pumpStatusMap.get(messageData.IotData.pump));

        // add device to the UI list
        const node = document.createElement('option');
        const nodeText = document.createTextNode(messageData.DeviceId);
        node.appendChild(nodeText);
        listOfDevices.appendChild(node);

        // if this is the first device being discovered, auto-select it
        if (needsAutoSelect) {
          needsAutoSelect = false;
          listOfDevices.selectedIndex = 0;
          OnSelectionChange();
        }
      }
      pumpLineChart.update();
      myLineChart.update();
    } catch (err) {
      console.error(err);
    }
  };
});
