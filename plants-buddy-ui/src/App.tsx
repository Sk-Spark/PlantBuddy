import './App.css';
import { useState } from 'react';
import Plot from 'react-plotly.js';
import {Modal} from '@fluentui/react';

function App() {
  const cosmosResp = readDataFromCosmos();

  cosmosResp.then((Resp) => {
    const x = Resp.map((item: any) => item.enqueuedTime);
    const y = Resp.map((item: any) => item.telemetry?.moisture);
    const temp = Resp.map((item: any) => item.telemetry?.temperature);
    const humidity = Resp.map((item: any) => item.telemetry?.humidity);
    const scatter = {
      x: x,
      y: y,
      name: 'Moisture',
      type: 'scatter',
      mode: 'lines+markers',
      marker: {color: 'red'},
    };
   
    const temperatureChart = {
      x: x,
      y: temp,
      name: 'Temperature',
      type: 'scatter',
      mode: 'lines+markers',
      marker: {color: 'blue'},
    };
    const humidityChart = {
      x: x,
      y: humidity,
      name: 'Humidity',
      type: 'scatter',
      mode: 'lines+markers',
      marker: {color: 'cyan'},
    };
    setData([scatter,temperatureChart,humidityChart]);

    const pumpChart = {
      x:x,
      y:Resp.map((item: any) => item.telemetry?.pumpStatus),
      type: 'scatter',
      mode: 'lines+markers',
      marker: {color: 'green'},      
    }
    setpumpData([pumpChart]);
    setTemp(temp[temp.length-1]);
    console.log("temp"+ temp[temp.length-1])
    console.log("moisture"+ y[y.length-1])
    console.log("humidity"+ humidity[humidity.length-1])
    setmoisture(y[y.length-1]);
    sethumidity(humidity[humidity.length-1]);
  });


  const [ data, setData ] = useState([{}]);
  const [ pumpdata, setpumpData ] = useState([{}]);
  const [temp,setTemp] = useState(0);
  const [moisture, setmoisture] = useState(0);
  const [humidity, sethumidity] = useState(0);


  return (
    <>
    <div>
   <Plot data={data}  layout={ {width: 500, height: 500, title: 'Temp(C), Humidity(%), Moisture(%)', xaxis:{title:"Time"}, yaxis:{title:"Temp, Humidity, Moisture"}, }} config={{scrollZoom:false}} />
   <Plot data={pumpdata} layout={ {width: 500, height: 500, title: 'Pump Status', xaxis:{title:"Time"}, yaxis:{title:"Pump Status"}}} config={{scrollZoom:false}}/>
<div>Temp - {temp}</div><div>Moisture - {moisture}</div><div>Humidity - {humidity}</div>

   </div>

   </>
   );

}

export default App;
async function readDataFromCosmos() {
  const endpoint = "https://plants-buddy.documents.azure.com:443/";
  const key = "cjUsp8q2luC3j49wYbEe1hshNm2nbVwj1efI30tnA7x7uEVSI0KrkZfEOaQC0kkmuPxs0ppO7y2zACDbEFEOvA==";
  const { CosmosClient } = require("@azure/cosmos");
  const client = new CosmosClient({ endpoint, key });
  const { database } = await client.databases.createIfNotExists({ id: "plants-buddy"});
  const { container } = await database.containers.createIfNotExists({
    id: "telemetry_db",
  });
  const { resources } = await container.items
  .query({
    query: `SELECT top 15* FROM c  WHERE DateTimeDiff("day", c.enqueuedTime, GetCurrentDateTime()) <= 90`
  })
  .fetchAll();
  return resources;
}