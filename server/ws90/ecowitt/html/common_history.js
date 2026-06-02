// ===========================================
//   COMMON HISTORY ENGINE (No ES Modules)
//   Safe for Cloudflare + Local Dashboards
// ===========================================

(function (global) {
    function buildHistoryURL(base, opts = {}) {
        const params = [];
        if (opts.days !== undefined && opts.days !== null)
            params.push(`days=${opts.days}`);
        if (opts.limit !== undefined && opts.limit !== null)
            params.push(`limit=${opts.limit}`);
        if (opts.offset !== undefined && opts.offset !== null)
            params.push(`offset=${opts.offset}`);
        return params.length ? `${base}?${params.join("&")}` : base;
    }

    async function fetchHistory(url) {
        const r = await fetch(url, { cache: "no-store" });
        if (!r.ok) throw new Error("HTTP " + r.status);
        return await r.json();
    }

    function normalizeRangeData(range) {
        return range.map((r) => ({
            t: new Date(r.time * 1000),
            min: r.min,
            max: r.max,
            mid: (r.min + r.max) / 2,
        }));
    }

    function autoScale(records) {
        if (!records.length) return { min: 0, max: 1 };

        const lows = records.map((r) => r.min);
        const highs = records.map((r) => r.max);

        const minVal = Math.min(...lows);
        const maxVal = Math.max(...highs);
        const pad = (maxVal - minVal) * 0.1 || 1;

        return { min: minVal - pad, max: maxVal + pad };
    }

    function renderHistoryChart(opts) {
        const { canvasId, records, range, unit } = opts;

        const canvas = document.getElementById(canvasId);
        const ctx = canvas.getContext("2d");

        const W = canvas.width;
        const H = canvas.height;

        ctx.clearRect(0, 0, W, H);

        const leftPad = 70;
        const rightPad = 20;
        const topPad = 20;
        const bottomPad = 30;

        const usableW = W - leftPad - rightPad;
        const usableH = H - topPad - bottomPad;

        const yScale = (val) =>
            topPad + (range.max - val) * (usableH / (range.max - range.min));

        const t0 = records[0].t.getTime();
        const t1 = records[records.length - 1].t.getTime();
        const xScale = (t) =>
            leftPad + ((t.getTime() - t0) / (t1 - t0)) * usableW;

        // Axes
        ctx.strokeStyle = "#333";
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(leftPad, topPad);
        ctx.lineTo(leftPad, H - bottomPad);
        ctx.lineTo(W - rightPad, H - bottomPad);
        ctx.stroke();

        // Draw min→max vertical bars
        ctx.strokeStyle = "#4db6ff88";
        ctx.lineWidth = 2;

        records.forEach((r) => {
            const x = xScale(r.t);
            ctx.beginPath();
            ctx.moveTo(x, yScale(r.min));
            ctx.lineTo(x, yScale(r.max));
            ctx.stroke();
        });

        // Draw midline
        ctx.strokeStyle = "#52a2ff";
        ctx.lineWidth = 2;
        ctx.beginPath();

        records.forEach((r, i) => {
            const x = xScale(r.t);
            const y = yScale(r.mid);
            if (i === 0) ctx.moveTo(x, y);
            else ctx.lineTo(x, y);
        });

        ctx.stroke();

        // =============================
        //  Month Divider Lines + Labels
        // =============================

        let lastMonth = -1;

        records.forEach((r) => {
            const dt = r.t;
            const month = dt.getMonth();
            const year = dt.getFullYear();

            if (month !== lastMonth) {
                lastMonth = month;

                const x = xScale(dt);

                // Draw faint vertical line
                ctx.strokeStyle = "#444";
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(x, topPad);
                ctx.lineTo(x, H - bottomPad);
                ctx.stroke();

                // Month-Year label
                const label =
                    dt.toLocaleString("default", { month: "short" }) +
                    " " +
                    year.toString().slice(-2);

                // Avoid label collisions near edges
                if (x > 35 && x < W - 35) {
                    ctx.fillStyle = "#bbb";
                    ctx.textAlign = "center";
                    ctx.font = "11px sans-serif";
                    ctx.fillText(label, x, H - 8);
                }
            }
        });

        // =============================
        //  Y-axis labels (draw last)
        // =============================
        ctx.fillStyle = "#ccc";
        ctx.textAlign = "right";
        ctx.font = "12px sans-serif";

        const ticks = 5;
        for (let i = 0; i <= ticks; i++) {
            const val = range.min + ((range.max - range.min) * i) / ticks;
            const y = yScale(val);
            ctx.fillText(val.toFixed(1) + unit, leftPad - 6, y + 4);
        }
    }

    // Expose API globally, safely
    global.CommonHistory = {
        buildHistoryURL,
        fetchHistory,
        normalizeRangeData,
        autoScale,
        renderHistoryChart,
    };
})(window);

window.exportTemperatureHistoryToCsv = function () {
    const data = window.tempHistoryData;

    if (!data || !data.days || data.days.length === 0) {
        alert("No temperature history data available.");
        return;
    }

    const rows = ["date,high_F,low_F"];

    for (const item of data.days) {
        const dateStr = new Date(item.day * 1000).toISOString().slice(0, 10);

        rows.push(
            `${dateStr},${item.temp_high_F ?? ""},${item.temp_low_F ?? ""}`,
        );
    }

    const blob = new Blob([rows.join("\n")], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "temperature_history.csv";
    a.click();
    URL.revokeObjectURL(url);
};

window.exportHumidityHistoryToCsv = function () {
    const data = window.humidityHistoryData;

    if (!data || !data.days || data.days.length === 0) {
        alert("No humidity history data available.");
        return;
    }

    const rows = ["date,humidity_high,humidity_low"];

    for (const item of data.days) {
        const dateStr = new Date(item.day * 1000).toISOString().slice(0, 10);

        rows.push(
            `${dateStr},${item.humidity_high ?? ""},${item.humidity_low ?? ""}`,
        );
    }

    const blob = new Blob([rows.join("\n")], { type: "text/csv" });
    const url = URL.createObjectURL(blob);
    const a = document.createElement("a");
    a.href = url;
    a.download = "humidity_history.csv";
    a.click();
    URL.revokeObjectURL(url);
};

window.exportRainHistoryToCsv = function () {
    const data = window.rainHistoryData;

    if (!data || !data.days || data.days.length === 0) {
        alert("No rain history data available.");
        return;
    }

    const rows = ["date,rain_in"];

    for (const item of data.days) {
        const dateStr = new Date(item.day * 1000).toISOString().slice(0, 10);
        rows.push(`${dateStr},${item.rain_in ?? ""}`);
    }

    const blob = new Blob([rows.join("\n")], { type: "text/csv" });
    const url = URL.createObjectURL(blob);

    const a = document.createElement("a");
    a.href = url;
    a.download = "rain_history.csv";
    a.click();

    URL.revokeObjectURL(url);
};
