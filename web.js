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
 * 2. แก้ไขส่วน FETCH DATA
 ***************************************/
async function fetchESP32Data() {
    try {
        const res = await fetch(`${ESP32_IP}/data`);
        const data = await res.json();

        // ประกาศตัวแปรอ้างอิง DOM
        const countsElement = document.getElementById('counts');
        const statusElement = document.getElementById('status');
        const bgElement = document.getElementById('bg-rate');
        const heightElement = document.getElementById('height');

        // 1. อัปเดตค่า Background Rate (เลขวิ่งเสมอเพื่อให้รู้ว่าเชื่อมต่ออยู่)
        if (data.bgRate !== undefined && bgElement) {
            const currentValue = parseFloat(bgElement.innerText) || 0;
            if (Math.abs(data.bgRate - currentValue) > 0.01) {
                animateValue('bg-rate', currentValue, data.bgRate, 1000);
            }
        }

        // 2. จัดการสถานะตามที่ได้รับจากบอร์ด
        
        // สถานะ IDLE: แยกเป็น "รอกดปุ่มที่จอ" กับ "สแกนเสร็จแล้ว"
        if (data.status === "idle") {
            if (heightData.length === 0) {
                // ยังไม่มีข้อมูลในกราฟ = รอกดปุ่มที่บอร์ด
                statusElement.innerText = "Waiting for LCD Setup...";
                countsElement.innerText = "0";
            } else {
                // มีข้อมูลแล้ว = สแกนเสร็จ มอเตอร์กลับบ้านแล้ว
                stopScan();
                statusElement.innerText = "Completed";
                countsElement.innerText = "--";
            }
            return; // จบรอบการทำงาน
        }

        // สถานะ MEASURING BG: กำลังนับถอยหลัง 20 วินาทีที่หน้าจอ
        if (data.status === "measuring_bg") {
            statusElement.innerText = "Measuring Background...";
            countsElement.innerText = "0"; // ล็อกค่า Net Count เป็น 0
            heightElement.innerText = "0";
            return; 
        }

        // สถานะ SCANNING: เริ่มสแกนจริง
        if (data.status === "scanning") {
            statusElement.innerText = "Scanning...";

            if (data.currentHeight !== undefined) {
                heightElement.innerText = data.currentHeight;
            }
        }

        // 3. ลอจิกบันทึกข้อมูลและพล็อตกราฟ (เมื่อวัดที่ความสูงนั้นๆ เสร็จ)
        if (data.finalHeight !== -1 && data.finalHeight !== currentScanHeight) {
              currentScanHeight = data.finalHeight;
              let finalNet = data.finalNetCount;

              // ✅ แสดง net count ของความสูงนี้
              countsElement.innerText = finalNet;
              heightElement.innerText = currentScanHeight;

              heightData.push(currentScanHeight);
              countData.push(finalNet);

              Plotly.extendTraces('countGraph', {
                  x: [[finalNet]],
                  y: [[currentScanHeight]]
              }, [0]);

              db.collection("scan_results").add({
                  sessionId: currentSessionId,
                  height: currentScanHeight,
                  counts: finalNet,
                  timestamp: firebase.firestore.FieldValue.serverTimestamp()
              });
          }

    } catch (err) {
        console.error("Connection Error:", err);
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