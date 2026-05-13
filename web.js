/***************************************
 * FIREBASE CONFIGURATION
 ***************************************/
// For Firebase JS SDK v7.20.0 and later, measurementId is optional
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
let ignoreFirstData = true;
let isNewSessionData = false; // ⭐ เพิ่มตัวแปรนี้

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
 * 1. เพิ่มฟังก์ชัน ANIMATION (ถ้าไม่มีอันนี้ เลขจะไม่วิ่ง)
 ***************************************/
function animateValue(id, start, end, duration) {
    const obj = document.getElementById(id);
    if (!obj) return;
    let startTimestamp = null;
    const step = (timestamp) => {
        if (!startTimestamp) startTimestamp = timestamp;
        const progress = Math.min((timestamp - startTimestamp) / duration, 1);
        obj.innerText = (progress * (end - start) + start).toFixed(2);
        if (progress < 1) {
            window.requestAnimationFrame(step);
        }
    };
    window.requestAnimationFrame(step);
}


/***************************************
 * FETCH DATA FROM ESP32
 ***************************************/
async function fetchESP32Data() {
  try {
    const res = await fetch(`${ESP32_IP}/data`);
    const data = await res.json();

    const countsElement = document.getElementById('counts');
    const statusElement = document.getElementById('status');
    const bgElement = document.getElementById('bg-rate');
    const heightElement = document.getElementById('height');

    // --- UPDATE BG RATE ---
    if (data.bgRate !== undefined && bgElement) {
      const currentValue = parseFloat(bgElement.innerText) || 0;
      if (Math.abs(data.bgRate - currentValue) > 0.01) {
        animateValue('bg-rate', currentValue, data.bgRate, 1000);
      }
    }

    // --- HANDLE STATUS ---
    if (data.status === "idle") {
      if (heightData.length === 0) {
        statusElement.innerText = "Waiting for LCD Setup...";
        countsElement.innerText = "0";
      } else {
        statusElement.innerText = "Completed";
        stopScan();
      }
      return;
    }

    if (data.status === "measuring_bg") {
      statusElement.innerText = "Measuring Background...";
      countsElement.innerText = "0";
      heightElement.innerText = "0";
      return;
    }

    if (data.status === "scanning") {
      statusElement.innerText = "Scanning...";
      
      // ✅ แก้จุดที่ 1: เปิดประตูรับข้อมูลทันทีที่สถานะเป็น scanning 
      // แต่ต้องมั่นใจว่า ESP32 เคลียร์ค่า finalHeight เป็น -1 หรือค่าเริ่มต้นแล้ว
      if (data.finalHeight === -1 || data.finalHeight === 0) {
          isNewSessionData = true; 
      }

      if (data.currentHeight !== undefined) {
        heightElement.innerText = data.currentHeight;
      }
      
      // ถ้ายังสแกนไม่เสร็จจุดแรก ให้เลข counts แสดง 0 ไว้ก่อน
      if (heightData.length === 0) {
        countsElement.innerText = "0";
      }
    }

    // --- SAVE DATA WHEN HEIGHT FINISHED ---
    // ✅ เพิ่มเงื่อนไข && isNewSessionData เพื่อกรองค่าเก่าจาก ESP32
    if (data.finalHeight !== undefined && data.finalHeight !== -1 && isNewSessionData) {
      // ✅ แก้จุดที่ 2: เช็คเพิ่มเติมว่าค่าที่ส่งมา "ต้องไม่มากกว่า" เป้าหมายที่กำลังสแกน (currentHeight) 
      // เพื่อป้องกันค่าเก่าจากปลายกราฟ (เช่น 60) กระโดดมาแสดงตอนเพิ่งเริ่มสแกนที่ 0
      if (data.finalHeight > data.currentHeight + 5) { // ถ้าค่าโดดเกินความสูงปัจจุบันไปมาก ให้ถือว่าเป็นค่าเก่า
          return;
      }

      let finalHeight = data.finalHeight;
      let finalNet = data.finalNetCount;

      // กันข้อมูลซ้ำในชั้นเดียวกัน
      if (finalHeight === currentScanHeight) {
        return;
      }

      currentScanHeight = finalHeight;

      console.log("New Data Received:", finalHeight, finalNet);

      countsElement.innerText = finalNet;
      heightElement.innerText = finalHeight;

      // SAVE GRAPH DATA
      heightData.push(finalHeight);
      countData.push(finalNet);

      Plotly.extendTraces('countGraph', {
        x: [[finalNet]],
        y: [[finalHeight]]
      }, [0]);

      // SAVE TO FIREBASE
      db.collection("scan_results").add({
        sessionId: currentSessionId,
        height: finalHeight,
        counts: finalNet,
        timestamp: firebase.firestore.FieldValue.serverTimestamp()
      })
      .then(() => { console.log("Saved to Firebase"); })
      .catch((error) => { console.error("Firebase Error:", error); });
    }

  } catch (err) {
    console.error("ESP32 Connection Error:", err);
    if (statusElement) {
      statusElement.innerText = "Reconnecting...";
    }
  }
}

/***************************************
 * CONTROL BUTTONS
 ***************************************/
function startScan() {
  if (scanInterval) return;

  currentSessionId = "session_" + new Date().getTime();
  isNewSessionData = false; // ⭐ สั่ง "ปิดประตู" ไม่รับค่า Final เก่า

  heightData = [];
  countData = [];
  currentScanHeight = -999; 
  
  document.getElementById('height').innerText = "0"; 
  document.getElementById('counts').innerText = "0"; 
  document.getElementById('status').innerText = "Initializing...";

  initGraphs();
  scanInterval = setInterval(fetchESP32Data, 1000);
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

  currentScanHeight = -1; 

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