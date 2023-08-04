module.exports = async function (context, mySbMsg) {
  const endpoint = process.env["COSMOS_DB_ENDPOINT"];
  const key = process.env["COSMOS_KEY"];

  const appinsightConnectionString = process.env["APPINSIGHTCONNECTIONSTRING"];
  const { v4: uuidv4 } = require("uuid");
  const { CosmosClient } = require("@azure/cosmos");
  const client = new CosmosClient({ endpoint, key });
  // const { ApplicationInsights } = require("@microsoft/applicationinsights-web");
  // const appInsights = new ApplicationInsights({
  //   config: {
  //     connectionString: appinsightConnectionString,
  //     /* ...Other Configuration Options... */
  //   },
  // });
  const { database } = await client.databases.createIfNotExists({
    id: "plants-buddy",
  });
  // appInsights.loadAppInsights();
  // appInsights.trackTrace({
  //   message: "PlantBuddy - Calling SendDataToCosmos Method",
  // });

  // Function call to send data to cosmos
  sendDataToCosmos(mySbMsg, database, uuidv4(), null);
};

async function sendDataToCosmos(data, database, id, appInsights) {
  // function to send data to cosmos db
  try {
    // appInsights.trackTrace({
    //   message: "PlantBuddy - In SendDataToCosmos Method",
    // });
    const dataToCosmos = {
      id,
      deviceId: `${data.deviceId}`,
      enqueuedTime: `${data.enqueuedTime}`,
      templateId: `${data.templateId}`,
      telemetry: {
        temperature: `${data.telemetry?.temperature}`,
        humidity: `${data.telemetry?.humidity}`,
        moisture: `${data.telemetry?.pot1_moisture_percent}`,
        moisture_sensor: `${data.telemetry?.pot1_moisture_sensore_value}`,
        pumpStatus: `${data.telemetry?.pump1}`,
        threshold: `${data.telemetry?.threshold1}`,
      },
    };

    const { container } = await database.containers.createIfNotExists({
      id: "telemetry_db",
    });
    // appInsights.trackTrace(
    //   { message: "PlantsBuddy - Creating Item in Cosmos Container" },
    //   { data: dataToCosmos }
    // );

    await container.items.create(dataToCosmos);
    console.log("Item created in cosmos");
  } catch (ex) {
    // appInsights.trackException({ exception: new Error(ex) });
  }
}
