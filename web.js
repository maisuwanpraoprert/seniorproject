/***************************************
 * FIREBASE CONFIGURATION
 ***************************************/
const firebaseConfig = {
  apiKey: "AIzaSyCPQ2bWH7HazW6s5-Y18dm2-qmcVlnWR40",
  authDomain: "columnscan.firebaseapp.com",
  projectId: "columnscan",
  storageBucket: "columnscan.firebasestorage.app",
  messagingSenderId: "853978035037",
  appId: "1:853978035037:web:fd33242fc88381749deeb4",
  measurementId: "G-7PH07M9BE2"
};

// Initialize Firebase (Compat)
firebase.initializeApp(firebaseConfig);
const db = firebase.firestore();

/***************************************
 * VARIABLES
 ***************************************/
let heightData = [];
let countData = [];
let scanInterval = null;
let currentSessionId = null; 

// ตัวแปรเสริมสำหรับกรองข้อมูล
let currentScanHeight = -1;
let highestCountForHeight = 0;

// ⚠️ สำคัญ: เวลาเทสจริง ต้องรันหน้าเว็บแบบ http://localhost หรือรันไฟล์ตรงๆ 
// ห้ามโฮสต์บน HTTPS ชั่วคราว เพราะเว็บบน HTTPS จะดึงข้อมูลจาก ESP32 (HTTP) ไม่ได้
const ESP32_IP = "http://172.20.10.2"; 

/***************************************
 * INIT GRAPH
 ***************************************/
function initGraphs() {
  Plotly.newPlot('countGraph', [{
    x: [],
    y: [],
    mode: 'lines+markers',
    name: 'Count Rate',
    line: { shape: 'spline', color: '#2610b4' }, // ปรับให้เส้นกราฟสมูทขึ้น
    marker: { size: 6 }
  }], {
    title: 'Density Profile',
    xaxis: { title: 'Net Counts (cps)'},
    yaxis: { title: 'Height (cm)'},
    width: 700,  
    height: 600,
  });
}

/***************************************
 * FETCH DATA & SAVE TO DATABASE
 ***************************************/
async function fetchESP32Data() {
    try {
      const res = await fetch(`${ESP32_IP}/data`);
      const data = await res.json();

      if (data.bgRate !== undefined) {
          document.getElementById('bg-rate').innerText = data.bgRate.toFixed(2);
      }
  
      // 🔥 1. เช็กว่าบอร์ดสแกนเสร็จหรือยัง ถ้าเสร็จแล้วให้หยุดดึงข้อมูลทันที!
      if (data.status === "idle") {
        stopScan(); // หยุด setInterval
        document.getElementById('status').innerText = "Completed";
        document.getElementById('counts').innerText = "--"; 
        return; // จบการทำงาน ไม่ต้องพล็อตกราฟต่อ
      }
  
      if (data.currentHeight === undefined) return;
  
      // 2. แสดงผลหน้าจอแบบ Real-time
      document.getElementById('height').innerText = data.currentHeight;
      document.getElementById('counts').innerText = data.liveRaw; 
      document.getElementById('status').innerText = "Scanning...";
  
      // 3. ลอจิกบันทึกกราฟ (บันทึกเฉพาะจุดที่นับเสร็จแล้วเท่านั้น)
      if (data.finalHeight !== -1 && data.finalHeight !== currentScanHeight) {
          currentScanHeight = data.finalHeight;
          let finalNet = data.finalNetCount;
  
          heightData.push(currentScanHeight);
          countData.push(finalNet);
  
          Plotly.extendTraces('countGraph', {
            x: [[finalNet]],
            y: [[currentScanHeight]]
          }, [0], 100);
  
          db.collection("scan_results").add({
            sessionId: currentSessionId,
            height: currentScanHeight,
            counts: finalNet,
            timestamp: firebase.firestore.FieldValue.serverTimestamp()
          });
      }
  
    } catch (err) {
      console.error("ESP32 connection error", err);
      document.getElementById('status').innerText = "Reconnecting...";
    }
}

/***************************************
 * CONTROL BUTTONS
 ***************************************/
function startScan() {
  if (scanInterval) return;

  currentSessionId = "session_" + new Date().getTime();
  heightData = [];
  countData = [];
  currentScanHeight = -1;       // รีเซ็ตตัวกรอง
  highestCountForHeight = 0;    // รีเซ็ตตัวกรอง
  
  initGraphs();

  fetch(`${ESP32_IP}/start`).catch(e => console.log("Start trigger error", e));
  
  // สั่งดึงข้อมูลทุกๆ 1 วินาที
  scanInterval = setInterval(fetchESP32Data, 1000);
  document.getElementById('status').innerText = "Initializing...";
}

function stopScan() {
  if (!scanInterval) return;
  fetch(`${ESP32_IP}/stop`).catch(e => console.log("Stop trigger error", e));
  clearInterval(scanInterval);
  scanInterval = null;
  document.getElementById('status').innerText = "Stopped";
}

function resetScan() {
  stopScan();
  heightData = [];
  countData = [];
  initGraphs();
  document.getElementById('height').innerText = "--";
  document.getElementById('counts').innerText = "--";
  document.getElementById('status').innerText = "Idle";
}

/***************************************
 * EXPORT CSV FUNCTION
 ***************************************/
function exportCSV() {
  if (heightData.length === 0) {
    alert("ไม่มีข้อมูลให้ Export");
    return;
  }

  let csvContent = "\uFEFFHeight (cm),Net Counts\n";
  for (let i = 0; i < heightData.length; i++) {
    csvContent += `${heightData[i]},${countData[i]}\n`;
  }

  const blob = new Blob([csvContent], { type: 'text/csv;charset=utf-8;' });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = `Scanner_Data_${new Date().getTime()}.csv`;
  link.click();
}

/***************************************
 * ON LOAD
 ***************************************/
window.onload = () => {
  initGraphs();
  
  const saveBtn = document.getElementById('saveBtn');
  if(saveBtn) saveBtn.onclick = saveGraph;
  
  const exportBtn = document.querySelector('.btn.export');
  if (exportBtn) exportBtn.onclick = exportCSV;
};

function saveGraph() {
  const graphDiv = document.getElementById('countGraph');
  const now = new Date();
  const dateStr = now.toLocaleDateString('th-TH').replace(/\//g, '-');
  const timeStr = now.getHours() + '-' + now.getMinutes();
  const fileName = `Profile_${dateStr}_${timeStr}`;

  Plotly.downloadImage(graphDiv, {
    format: 'png',
    width: 450,
    height: 600,
    filename: fileName
  });
}