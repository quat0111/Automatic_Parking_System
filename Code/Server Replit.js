const express = require("express");
const app = express();
const cors = require("cors");

app.use(cors());
app.use(express.json());

let lastTransaction = null;

// Route webhook nhận dữ liệu từ SEPay
app.post("/webhook", (req, res) => {
  console.log("Received webhook:", req.body);
  lastTransaction = req.body;
  res.sendStatus(200);
});

// Route cho ESP32 truy cập để lấy giao dịch mới nhất
app.get("/latest", (req, res) => {
  if (lastTransaction) {
    res.json(lastTransaction);
  } else {
    res.status(404).json({ message: "No transaction yet" });
  }
});

const PORT = process.env.PORT || 3000;
app.listen(PORT, () => {
  console.log(`Server running on port ${PORT}`);
});
