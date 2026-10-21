% irrig_sim.m — replika state machine irigasi firmware/src/irrigation.cpp
%
% Jalankan (dari akar repo):
%   octave --no-gui --eval "addpath('firmware/tools'); irrig_sim"
%
% Semua logika di atas berjalan di perangkat; ini salinan yang bisa dijalankan
% di host supaya perubahan irrigation.cpp punya bukti logika (bukan hanya
% "kelihatan benar"). Skenario diuji dengan assert, jadi regresi di
% irrigation.cpp langsung ketahuan tanpa perlu device.
%
% Prototipe sekarang memakai SATU pompa (DIST): siklus IDLE -> DIST -> IDLE.
% Dosis air per siklus dihitung dari ETc (Blaney-Criddle):
%   anggaran_harian_ml = ETc * IRRIG_AREA_CM2 * 0,1
%   dosis_ml           = anggaran_harian_ml / IRRIG_CYCLES_PER_DAY
%   durasi_pompa_ms    = dosis_ml / PUMP_DIST_ML_PER_S * 1000
%   jeda_minimum_ms    = 24 jam / IRRIG_CYCLES_PER_DAY
%
% Skenario yang diuji:
%   (a) ETc 8,68 -> siklus pertama jalan dengan durasi 123 s
%   (b) permintaan kedua dalam 1 menit DITAHAN oleh jeda minimum (12 jam)
%   (c) setelah jeda minimum lewat, siklus kedua jalan
%   (d) setelah 2 siklus, siklus ketiga DITAHAN oleh anggaran harian (7812 mL)
%   (e) ETc turun ke 7,02 -> dosis & durasi ikut turun (99,7 s)
%   (f) ETc naik ke 11,51 (fase pembungaan) -> dosis 5180 mL, durasi 163,4 s
%   (g) soil invalid -> tidak menyiram
%   (h) manual override -> siklus boleh jalan
%   (i) satu permintaan = satu siklus (tidak ada siklus ganda)

function irrig_sim()

%% --- Parameter (salinan config.h, sudah terukur 21 Okt 2026) --------------
cfg.PUMP_DIST_ML_PER_S   = 31.7;    % mL/s (gelas ukur + stopwatch, 3x, rata-rata)
cfg.IRRIG_AREA_CM2       = 9000;    % 10 polybag 30x30 cm -> 900 cm2 per tanaman
cfg.IRRIG_CYCLES_PER_DAY = 2;
cfg.STEP_MS              = 2000;    % periode controlTask

%% --- Tabel acuan: ETc -> anggaran harian -> dosis -> durasi ------------
% Angka acuan yang harus keluar dari perhitungan (A_total 9000 cm2,
% 2 siklus/hari, debit 31,7 mL/s).
ref = [7.02  6318  3159   99.7;
       7.85  7065  3533  111.4;
       8.68  7812  3906  123.2;
      11.51 10359  5180  163.4];
for k = 1:rows(ref)
  assert(near(daily_budget_ml(cfg, ref(k,1)), ref(k,2), 1), ...
         'tabel acuan: anggaran harian ETc %.2f != %.0f mL', ref(k,1), ref(k,2));
  assert(near(dose_ml(cfg, ref(k,1)), ref(k,3), 1), ...
         'tabel acuan: dosis ETc %.2f != %.0f mL', ref(k,1), ref(k,3));
  assert(near(dose_ms(cfg, ref(k,1)) / 1000, ref(k,4), 0.2), ...
         'tabel acuan: durasi ETc %.2f != %.1f s', ref(k,1), ref(k,4));
endfor
assert(min_interval_ms(cfg) == 43200000, 'tabel acuan: jeda minimum bukan 12 jam');
printf('Tabel acuan ETc -> anggaran -> dosis -> durasi COCOK (4 baris, jeda 12 jam)\n\n');

%% --- Fungsi pembantu (nested: Octave tidak meng-hoist fungsi skrip) ------
function st = next_phase(st, p)
  st.phase = p;
  st.phaseStart = st.now;
endfunction

function budget = daily_budget_ml(cfg, etcv)
  % 1 mm x 1 cm2 = 0,1 mL
  if etcv <= 0
    budget = 0;
  else
    budget = etcv * cfg.IRRIG_AREA_CM2 * 0.1;
  endif
endfunction

function dose = dose_ml(cfg, etcv)
  dose = daily_budget_ml(cfg, etcv) / cfg.IRRIG_CYCLES_PER_DAY;
endfunction

function ms = dose_ms(cfg, etcv)
  ms = dose_ml(cfg, etcv) / cfg.PUMP_DIST_ML_PER_S * 1000;
endfunction

function ms = min_interval_ms(cfg)
  ms = 86400000 / cfg.IRRIG_CYCLES_PER_DAY;
endfunction

function st = roll_over_day(st)
  day = floor(st.now / 86400000);
  if !st.dayInit
    st.dayInit = true;
    st.dayIndex = day;
    st.usedMl = 0;
  elseif day != st.dayIndex
    st.dayIndex = day;
    st.usedMl = 0;
    st.log{end+1} = 'hari berganti: anggaran direset';
  endif
endfunction

function st = irr_update(st, cfg, trigger, etcv)
  st = roll_over_day(st);

  if trigger && st.phase == 0
    budget = daily_budget_ml(cfg, etcv);
    dose = dose_ml(cfg, etcv);
    dms = dose_ms(cfg, etcv);

    if dose <= 0 || dms <= 0
      st.log{end+1} = 'ETc belum tersedia, irigasi ditahan';
    elseif st.usedMl + dose > budget + 0.5
      st.log{end+1} = sprintf('anggaran harian habis: %.0f/%.0f mL', st.usedMl, budget);
    elseif st.hasCycle && (st.now - st.lastCycleStartMs) < min_interval_ms(cfg)
      st.log{end+1} = 'jeda minimum belum lewat';
    else
      st = next_phase(st, 1);              % DIST
      st.cycleDoseMs = floor(dms);         % sama dengan konversi unsigned long di firmware
      st.cycleDoseMl = dose;
      st.lastCycleStartMs = st.now;
      st.hasCycle = true;
      st.usedMl += dose;
      st.log{end+1} = sprintf(['irigasi: dose %.0f mL -> %.0f s | jeda min %.0f jam | ' ...
                                'anggaran harian %.0f mL, terpakai %.0f mL'], ...
                               dose, dms/1000, min_interval_ms(cfg)/3600000, budget, st.usedMl);
    endif
  endif

  if st.phase == 1
    st.pumpOn = true;
    if st.now - st.phaseStart >= st.cycleDoseMs
      st.pumpOn = false;
      st = next_phase(st, 0);
      st.log{end+1} = 'siklus selesai';
    endif
  endif
endfunction

function st = irr_abort(st)
  st.pumpOn = false;
  st = next_phase(st, 0);
endfunction

function st = init_state()
  st.phase = 0;                  % 0 IDLE, 1 DIST
  st.phaseStart = 0;
  st.now = 0;
  st.usedMl = 0;
  st.dayIndex = 0;
  st.dayInit = false;
  st.hasCycle = false;
  st.lastCycleStartMs = 0;
  st.cycleDoseMs = 0;
  st.cycleDoseMl = 0;
  st.pumpOn = false;
  st.log = {};
endfunction

function n = count_log(st, what)
  n = sum(strcmp(st.log, what));
endfunction

function n = count_prefix(st, pref)
  n = 0;
  for k = 1:numel(st.log)
    if strncmp(st.log{k}, pref, length(pref))
      n++;
    endif
  endfor
endfunction

function ok = near(a, b, tol)
  ok = abs(a - b) <= tol;
endfunction

% Logika .ino: soil invalid -> keputusan fuzzy tidak dipakai; override manual
% operator menang (perintah eksplisit).
function trigger = decide_trigger(soilValid, fuzzyWants, manualForced)
  if manualForced
    trigger = true;
  else
    trigger = soilValid && fuzzyWants;
  endif
endfunction

%% --- (a) ETc 8,68 -> siklus pertama, durasi 123 s -----------------------
st = init_state();
etcv = 8.68;
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);
assert(st.phase == 1, '(a) siklus tidak dimulai');
assert(near(dose_ml(cfg, etcv), 3906, 1), '(a) dosis bukan 3906 mL');
assert(near(dose_ms(cfg, etcv) / 1000, 123.2, 0.2), '(a) durasi bukan 123,2 s');
assert(st.cycleDoseMs == floor(dose_ms(cfg, etcv)), '(a) durasi siklus tidak terkunci');
assert(near(st.usedMl, 3906, 1), '(a) volume terpakai bukan 3906 mL');
assert(count_prefix(st, 'irigasi: dose') == 1, '(a) log siklus tidak ada');
printf('(a) ETc 8,68 -> dose 3906 mL, durasi 123 s, terpakai 3906 mL      OK\n');

%% --- (b) permintaan kedua dalam 1 menit DITAHAN (siklus masih jalan) ----
st.now += 60000;                                  % 1 menit kemudian
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);
assert(st.phase == 1, '(b) siklus berhenti saat permintaan kedua datang');
assert(count_prefix(st, 'irigasi: dose') == 1, '(b) ada siklus kedua saat siklus pertama jalan');
assert(st.pumpOn, '(b) pompa DIST mati saat siklus berjalan');
printf('(b1) permintaan 1 menit kemudian: siklus lama jalan, tak ada siklus ganda  OK\n');

%% --- (b2) permintaan setelah siklus selesai, jeda minimum masih belum ----
st.now += 180000;                                 % siklus (123 s) sudah selesai
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);   % menutup siklus
assert(st.phase == 0, '(b2) siklus belum selesai');
st.now += cfg.STEP_MS;
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);   % permintaan baru
assert(st.phase == 0, '(b2) siklus jalan padahal jeda minimum belum lewat');
assert(count_prefix(st, 'jeda minimum belum lewat') == 1, '(b2) log jeda minimum tidak ada');
assert(near(st.usedMl, 3906, 1), '(b2) volume terpakai berubah padahal siklus ditahan');
printf('(b2) permintaan setelah siklus selesai DITAHAN jeda minimum 12 jam    OK\n');

%% --- (c) setelah jeda minimum lewat, siklus kedua jalan -----------------
st.now += min_interval_ms(cfg);                   % +12 jam dari siklus pertama
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);
assert(st.phase == 1, '(c) siklus kedua tidak jalan setelah jeda minimum');
assert(count_prefix(st, 'irigasi: dose') == 2, '(c) siklus kedua tidak tercatat');
assert(near(st.usedMl, 7812, 2), '(c) volume terpakai bukan 7812 mL');
printf('(c) setelah jeda 12 jam, siklus kedua jalan (terpakai 7812 mL)    OK\n');

%% --- (d) siklus ketiga DITAHAN oleh anggaran harian ---------------------
st.now += 180000;                                 % siklus kedua selesai
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);
st.now += cfg.STEP_MS;
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);   % permintaan ketiga
assert(count_prefix(st, 'anggaran harian habis') == 1, '(d) log anggaran habis tidak ada');
assert(near(st.usedMl, 7812, 2), '(d) volume terpakai berubah padahal ditahan');
printf('(d) siklus ketiga DITAHAN: anggaran harian 7812/7812 mL           OK\n');

%% --- (e) ETc turun ke 7,02 -> dosis & durasi ikut turun ------------------
etcv = 7.02;
assert(near(daily_budget_ml(cfg, etcv), 6318, 1), '(e) anggaran harian bukan 6318 mL');
assert(near(dose_ml(cfg, etcv), 3159, 1), '(e) dosis bukan 3159 mL');
assert(near(dose_ms(cfg, etcv) / 1000, 99.7, 0.2), '(e) durasi bukan 99,7 s');
st.now += min_interval_ms(cfg);
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv);
assert(st.phase == 1, '(e) siklus dengan ETc turun tidak jalan');
assert(near(st.cycleDoseMl, 3159, 1), '(e) dosis siklus bukan 3159 mL');
printf('(e) ETc 7,02 -> dose 3159 mL, durasi 99,7 s                       OK\n');

%% --- (f) ETc naik ke 11,51 (fase pembungaan) -> dose & durasi naik ------
etcv = 11.51;
assert(near(daily_budget_ml(cfg, etcv), 10359, 1), '(f) anggaran harian bukan 10359 mL');
assert(near(dose_ml(cfg, etcv), 5180, 1), '(f) dosis bukan 5180 mL');
assert(near(dose_ms(cfg, etcv) / 1000, 163.4, 0.2), '(f) durasi bukan 163,4 s');
printf('(f) ETc 11,51 -> dose 5180 mL, durasi 163,4 s                     OK\n');

%% --- (g) soil invalid -> tidak menyiram ---------------------------------
st = init_state();
st = irr_update(st, cfg, decide_trigger(false, true, false), etcv);
assert(st.phase == 0, '(g) siklus jalan padahal soil invalid');
assert(st.usedMl == 0, '(g) volume terpakai berubah padahal tidak menyiram');
printf('(g) soil invalid -> tidak menyiram                               OK\n');

%% --- (h) manual override -> siklus boleh jalan --------------------------
st = init_state();
st = irr_update(st, cfg, decide_trigger(false, true, true), etcv);
assert(st.phase == 1, '(h) override manual tidak menjalankan siklus');
assert(st.pumpOn, '(h) pompa DIST tidak hidup');
printf('(h) override manual -> siklus jalan (pompa DIST hidup)             OK\n');

%% --- (i) satu permintaan = satu siklus (tidak ada siklus ganda) ---------
st = init_state();
for k = 1:100                                     % 100 x 2 s = 200 s
  st.now += cfg.STEP_MS;
  st = irr_update(st, cfg, decide_trigger(true, true, false), 8.68);
endfor
assert(count_prefix(st, 'irigasi: dose') == 1, ...
       '(i) permintaan tunggal menghasilkan %d siklus', count_prefix(st, 'irigasi: dose'));
assert(st.phase == 0, '(i) pompa masih jalan setelah siklus selesai');
printf('(i) permintaan terus TRUE: tepat 1 siklus (tidak ada siklus ganda)  OK\n');

printf('\nSEMUA SKENARIO STATE MACHINE IRIGASI LOLOS\n');
endfunction
