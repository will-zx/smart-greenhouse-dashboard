% irrig_sim.m — replika state machine fertigasi firmware/src/irrigation.cpp
%
% Jalankan (dari akar repo):
%   octave --no-gui --eval "addpath('firmware/tools'); irrig_sim"
%
% Semua logika di atas berjalan di perangkat; ini salinan yang bisa dijalankan
% di host supaya perubahan irrigation.cpp punya bukti logika (bukan hanya
% "kelihatan benar"). Skenario diuji dengan assert, jadi regresi di
% irrigation.cpp langsung ketahuan tanpa perlu device.
%
% Siklus enam fase (empat pompa):
%   IDLE -> FILL -> FERT -> SETTLE -> CHECK -> DIST -> IDLE
%   FILL   : penampungan -> ember campuran (PUMP_FILL)
%   FERT   : stok A + B -> ember (PUMP_FERT_A/B), durasi tetap (lingkaran terbuka)
%   SETTLE : semua pompa mati, larutan homogen sebelum TDS dibaca
%   CHECK  : nilai TDS. TERBACA di luar rentang -> DIST DITAHAN (siklus
%            dibatalkan, anggaran harian dikembalikan). Probe TIDAK terbaca ->
%            air tetap dialirkan (tanpa probe tanaman tidak akan pernah disiram)
%   DIST   : ember -> tanaman (PUMP_DIST) selama dosis hasil ETc
%
% Dosis air per siklus dihitung dari ETc (Blaney-Criddle, p dari tabel FAO-24):
%   anggaran_harian_ml = ETc * IRRIG_AREA_CM2 * 0,1
%   dosis_ml           = anggaran_harian_ml / IRRIG_CYCLES_PER_DAY
%   durasi_pompa_ms    = dosis_ml / PUMP_DIST_ML_PER_S * 1000
%   jeda_minimum_ms    = 24 jam / IRRIG_CYCLES_PER_DAY
%
% Skenario yang diuji:
%   (a) ETc 4,633 (data device 25 Okt) -> urutan enam fase benar, dosis 2085 mL
%   (b) permintaan saat siklus berjalan -> tidak ada siklus ganda
%   (b2) siklus selesai, jeda 12 jam belum lewat -> DITAHAN
%   (c) setelah jeda 12 jam -> siklus kedua jalan (anggaran penuh 4170 mL)
%   (d) siklus ketiga DITAHAN oleh anggaran harian
%   (e) ETc 7,02 -> dosis 3159 mL, durasi 99,7 s
%   (f) ETc 11,51 -> dosis 5180 mL, durasi 163,4 s
%   (g) soil invalid -> tidak menyiram
%   (h) manual override -> siklus boleh jalan
%   (i) trigger terus TRUE -> tepat satu siklus
%   (j) permintaan manual BARU menembus jeda minimum
%   (k) override manual tetap menyala -> tidak ada siklus kedua
%   (l) override dilepas lalu dinyalakan lagi -> siklus baru boleh jalan
%   (m) TDS dalam rentang -> DIST jalan
%   (n) TDS TERBACA di luar rentang -> DIST tidak pernah jalan, anggaran kembali
%   (n2) TDS di bawah batas bawah -> juga ditahan
%   (o) TDS tidak terbaca (NaN) -> DIST tetap jalan + peringatan
%   (p) setelah siklus ditahan TDS: trigger TRUE tidak memicu FILL berulang,
%       permintaan manual baru boleh mencoba lagi
%   (q) siklus ditahan TDS berhenti di fase IDLE (tidak menggantung di DIST)

function irrig_sim()

%% --- Parameter (salinan config.h, terukur 21 Okt 2026) --------------------
cfg.PUMP_DIST_ML_PER_S    = 31.7;    % mL/s (gelas ukur + stopwatch, 3x, rata-rata)
cfg.IRRIG_AREA_CM2        = 9000;    % 10 polybag 30x30 cm -> 900 cm2 per tanaman
cfg.IRRIG_CYCLES_PER_DAY  = 2;
cfg.STEP_MS               = 2000;    % periode controlTask
cfg.IRRIG_FILL_MS         = 157728;  % 5000 mL / 31,7 mL/s
cfg.IRRIG_FERT_MS         = 1126;    % 25 mL stok / 22,2 mL/s
cfg.IRRIG_SETTLE_MS       = 30000;   % TODO: ukur lapangan
cfg.IRRIG_CHECK_MS        = 2000;
cfg.IRRIG_PHASE_TIMEOUT_MS = 180000;
cfg.TDS_MIN_PPM           = 800;
cfg.TDS_MAX_PPM           = 1200;

% Nomor fase = urutan enum IrrigPhase di irrigation.h
FILL = 1; FERT = 2; SETTLE = 3; CHECK = 4; DIST = 5; IDLE = 0;

%% --- Tabel acuan: ETc -> anggaran harian -> dosis -> durasi ---------------
% Angka acuan yang harus keluar dari perhitungan (A_total 9000 cm2,
% 2 siklus/hari, debit 31,7 mL/s). Baris terakhir = data device 25 Okt 2026
% (p Oktober 0,280, Tmean 30,6 C, Kc 0,75 -> ETc 4,633).
ref = [ 4.633  4169  2085   65.8;
        7.02   6318  3159   99.7;
        7.85   7065  3533  111.4;
        8.68   7812  3906  123.2;
       11.51  10359  5180  163.4];
for ci = 1:rows(ref)
  assert(near(daily_budget_ml(cfg, ref(ci,1)), ref(ci,2), 1), ...
         'tabel acuan: anggaran harian ETc %.3f != %.0f mL', ref(ci,1), ref(ci,2));
  assert(near(dose_ml(cfg, ref(ci,1)), ref(ci,3), 1), ...
         'tabel acuan: dosis ETc %.3f != %.0f mL', ref(ci,1), ref(ci,3));
  assert(near(dose_ms(cfg, ref(ci,1)) / 1000, ref(ci,4), 0.2), ...
         'tabel acuan: durasi ETc %.3f != %.1f s', ref(ci,1), ref(ci,4));
endfor
assert(min_interval_ms(cfg) == 43200000, 'tabel acuan: jeda minimum bukan 12 jam');
printf('Tabel acuan ETc -> anggaran -> dosis -> durasi COCOK (5 baris, jeda 12 jam)\n\n');

%% --- Fungsi pembantu (nested: Octave tidak meng-hoist fungsi skrip) -------
% Nama variabel loop di helper sengaja beda dari loop utama (ci/rj/rk) karena
% nested function berbagi lingkup dengan badan fungsi induk.
function st = go(st, p)
  % Ganti fase + nyalakan pompa fase itu (padanan enterPhase() di firmware).
  % Pompa dinyalakan saat MASUK fase, bukan saat keluar, supaya fase yang
  % lebih pendek dari satu tick tetap benar-benar menggerakkan pompa.
  if p == st.phase
    return;
  endif
  st.phase = p;
  st.phaseStart = st.now;
  st.phaseLog{end+1} = phase_name(p);
  st.tLog(end+1) = st.now;
  switch p
    case 1
      st = set_pumps(st, true, false, false);      % FILL
    case 2
      st = set_pumps(st, false, true, false);      % FERT (A+B)
    case 5
      st = set_pumps(st, false, false, true);      % DIST
    otherwise
      st = set_pumps(st, false, false, false);     % SETTLE, CHECK, IDLE
  endswitch
endfunction

function n = phase_name(p)
  names = {'IDLE', 'FILL', 'FERT', 'SETTLE', 'CHECK', 'DIST'};
  n = names{p + 1};
endfunction

function st = set_pumps(st, fill, fert, dist)
  % Padanan setPumps(): satu grup menyala, sisanya pasti mati.
  assert(!(fill && (fert || dist)) && !(fert && dist), ...
         'dua grup pompa menyala bersamaan');
  st.fillOn = fill;
  st.fertOn = fert;
  st.distOn = dist;
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

% Padanan logReject(): alasan yang sama dilaporkan maksimal 1x per 60 detik.
function st = reject(st, key, msg)
  if strcmp(st.rejectKey, key) && (st.now - st.rejectMs) < 60000
    return;
  endif
  st.rejectKey = key;
  st.rejectMs = st.now;
  st.log{end+1} = msg;
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

function st = irr_update(st, cfg, trigger, etcv, manual, tdsPpm, tdsValid)
  st = roll_over_day(st);

  % Permintaan manual BARU = sakelar operator baru dinyalakan (turun = dilepas).
  manualNew = manual && !st.prevManual;
  st.prevManual = manual;
  if !manual
    st.manualRequestUsed = false;
  endif

  if trigger && st.phase == 0
    budget = daily_budget_ml(cfg, etcv);
    dose = dose_ml(cfg, etcv);
    dms = dose_ms(cfg, etcv);

    budgetFull = (st.usedMl + dose > budget + 0.5);
    intervalShort = st.hasCycle && (st.now - st.lastCycleStartMs) < min_interval_ms(cfg);
    % Manual boleh menembus jeda & anggaran, tapi satu siklus per permintaan.
    manualAllow = manual && manualNew && !st.manualRequestUsed;

    if dose <= 0 || dms <= 0
      st.holdReason = 'ETc belum tersedia';
      st = reject(st, 'etc', 'ETc belum tersedia, irigasi ditahan');
    elseif budgetFull && !manualAllow
      st.holdReason = 'anggaran harian habis';
      st = reject(st, 'budget', sprintf('anggaran harian habis: %.0f/%.0f mL', ...
                                        st.usedMl, budget));
    elseif intervalShort && !manualAllow
      if !st.tdsBlocked            % jangan timpa alasan TDS (lebih penting)
        st.holdReason = 'jeda minimum belum lewat';
        st = reject(st, 'jeda', 'jeda minimum belum lewat');
      endif
    else
      if manualAllow && (budgetFull || intervalShort)
        st.log{end+1} = 'override manual: siklus jalan di luar jeda/anggaran (niat operator)';
      endif
      if manualAllow
        st.manualRequestUsed = true;
      endif
      st.holdReason = '';
      st.tdsBlocked = false;             % siklus baru: alasan TDS lama selesai
      st.cycleDoseMs = floor(dms);       % sama dengan konversi unsigned long di firmware
      st.cycleDoseMl = dose;
      st.lastCycleStartMs = st.now;
      st.hasCycle = true;
      st.usedMl += dose;                 % volume terkumpul hari ini
      st.log{end+1} = sprintf(['irigasi: dose %.0f mL -> %.0f s | jeda min %.0f jam | ' ...
                                'anggaran harian %.0f mL, terpakai %.0f mL'], ...
                               dose, dms/1000, min_interval_ms(cfg)/3600000, budget, st.usedMl);
      st = go(st, 1);                    % FILL dulu, bukan langsung DIST
    endif
  endif

  switch st.phase
    case 1                              % FILL: pompa sudah menyala sejak masuk
      if st.now - st.phaseStart >= cfg.IRRIG_FILL_MS || ...
         st.now - st.phaseStart >= cfg.IRRIG_PHASE_TIMEOUT_MS
        st = go(st, 2);
      endif
    case 2                              % FERT: stok A + B sudah menyala
      if st.now - st.phaseStart >= cfg.IRRIG_FERT_MS || ...
         st.now - st.phaseStart >= cfg.IRRIG_PHASE_TIMEOUT_MS
        st = go(st, 3);
      endif
    case 3                              % SETTLE: semua pompa mati
      if st.now - st.phaseStart >= cfg.IRRIG_SETTLE_MS || ...
         st.now - st.phaseStart >= cfg.IRRIG_PHASE_TIMEOUT_MS
        st = go(st, 4);
      endif
    case 4                              % CHECK: nilai TDS sebelum DIST
      if st.now - st.phaseStart >= cfg.IRRIG_CHECK_MS
        if tdsValid && (tdsPpm < cfg.TDS_MIN_PPM || tdsPpm > cfg.TDS_MAX_PPM)
          % Larutan TERBACA dan jelas salah: tahan, jangan siram.
          st.holdReason = 'TDS di luar rentang, distribusi ditahan';
          st = reject(st, 'tds_hold', ...
                      sprintf(['TDS %.0f ppm di luar %.0f..%.0f ppm: distribusi ' ...
                               'DITAHAN, siklus dibatalkan'], ...
                              tdsPpm, cfg.TDS_MIN_PPM, cfg.TDS_MAX_PPM));
          st.usedMl = max(0, st.usedMl - st.cycleDoseMl);  % air tak jadi dialirkan
          st.cycleDoseMs = 0;
          st.cycleDoseMl = 0;
          st.tdsBlocked = true;      % alasan dipertahankan sampai siklus berikutnya
          st = go(st, 0);
          st.log{end+1} = 'siklus dibatalkan (TDS)';
        else
          if !tdsValid
            % Probe tidak terbaca: peringatan saja, air tetap dialirkan.
            st.holdReason = 'TDS tidak terbaca (air tetap dialirkan)';
            st = reject(st, 'tds', 'TDS tidak terbaca: air tetap didistribusikan');
          else
            st.log{end+1} = sprintf('TDS %.0f ppm lolos (%.0f..%.0f ppm)', ...
                                    tdsPpm, cfg.TDS_MIN_PPM, cfg.TDS_MAX_PPM);
          endif
          st = go(st, 5);
        endif
      endif
    case 5                              % DIST: pompa sudah menyala sejak masuk
      if st.now - st.phaseStart >= st.cycleDoseMs
        st = go(st, 0);
        st.log{end+1} = 'siklus selesai';
      endif
    otherwise
      % IDLE: pompa sudah dimatikan oleh go().
  endswitch

  st.snap(end+1,:) = [st.phase, st.fillOn, st.fertOn, st.distOn, st.now];
endfunction

% Majukan waktu `ms` dengan langkah controlTask (2 s).
function st = run_for(st, cfg, ms, trigger, etcv, manual, tdsPpm, tdsValid)
  tEnd = st.now + ms;
  while st.now < tEnd
    st.now += cfg.STEP_MS;
    st = irr_update(st, cfg, trigger, etcv, manual, tdsPpm, tdsValid);
  endwhile
endfunction

function st = step(st, cfg, trigger, etcv, manual, tdsPpm, tdsValid)
  st.now += cfg.STEP_MS;
  st = irr_update(st, cfg, trigger, etcv, manual, tdsPpm, tdsValid);
endfunction

function st = init_state()
  st.phase = 0;
  st.phaseStart = 0;
  st.now = 0;
  st.usedMl = 0;
  st.dayIndex = 0;
  st.dayInit = true;              % hari sudah diketahui; reset hanya saat berganti
  st.hasCycle = false;
  st.lastCycleStartMs = 0;
  st.cycleDoseMs = 0;
  st.cycleDoseMl = 0;
  st.fillOn = false;
  st.fertOn = false;
  st.distOn = false;
  st.prevManual = false;
  st.manualRequestUsed = false;
  st.holdReason = '';
  st.rejectKey = '';             % throttle log penolakan (1x/60 s per alasan)
  st.rejectMs = 0;
  st.tdsBlocked = false;         % alasan tahan TDS dipertahankan sampai siklus baru
  st.log = {};
  st.phaseLog = {};
  st.tLog = [];
  st.snap = zeros(0, 5);
endfunction

function n = count_prefix(st, pref)
  n = 0;
  for rk = 1:numel(st.log)
    if strncmp(st.log{rk}, pref, length(pref))
      n = n + 1;
    endif
  endfor
endfunction

function ok = near(a, b, tol)
  ok = abs(a - b) <= tol;
endfunction

function ok = phase_seq(st, want)
  ok = numel(st.phaseLog) == numel(want) && ...
       all(cellfun(@(a, b) strcmp(a, b), st.phaseLog, want));
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

%% --- (a) siklus penuh enam fase, ETc data device 4,633 -------------------
st = init_state();
etcv = 4.633;
tdsOk = 1000;
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);
assert(st.phase == 1, '(a) siklus tidak dimulai di fase FILL');
assert(st.fillOn && !st.fertOn && !st.distOn, '(a) pompa FILL tidak menyala sendiri');
assert(near(dose_ml(cfg, etcv), 2085, 1), '(a) dosis bukan 2085 mL');
assert(near(dose_ms(cfg, etcv) / 1000, 65.8, 0.2), '(a) durasi bukan 65,8 s');
assert(st.cycleDoseMs == floor(dose_ms(cfg, etcv)), '(a) durasi siklus tidak terkunci');
assert(near(st.usedMl, 2085, 1), '(a) volume terpakai bukan 2085 mL');

st = run_for(st, cfg, 400000, true, etcv, false, tdsOk, true);   % 400 s > satu siklus
assert(phase_seq(st, {'FILL', 'FERT', 'SETTLE', 'CHECK', 'DIST', 'IDLE'}), ...
       '(a) urutan fase bukan FILL->FERT->SETTLE->CHECK->DIST->IDLE (%s)', ...
       strjoin(st.phaseLog, '->'));
assert(st.phase == 0 && !st.fillOn && !st.fertOn && !st.distOn, ...
       '(a) pompa masih menyala setelah siklus selesai');
% Pompa FILL hanya hidup di fase FILL, pompa DIST hanya hidup di fase DIST.
assert(all(st.snap(st.snap(:,1) == 1, 2) == 1), '(a) FILL tidak menyalakan pompa FILL');
assert(all(st.snap(st.snap(:,1) ~= 1, 2) == 0), '(a) pompa FILL hidup di luar fase FILL');
assert(all(st.snap(st.snap(:,1) == 5, 4) == 1), '(a) DIST tidak menyalakan pompa DIST');
assert(all(st.snap(st.snap(:,1) ~= 5, 4) == 0), '(a) pompa DIST hidup di luar fase DIST');
assert(all(st.snap(st.snap(:,1) == 2, 3) == 1), '(a) FERT tidak menyalakan pompa stok');
assert(all(all(st.snap(st.snap(:,1) == 3 | st.snap(:,1) == 4, 2:4) == 0)), ...
       '(a) ada pompa menyala saat SETTLE/CHECK');
durS = (st.tLog(6) - st.tLog(1)) / 1000;               % FILL start -> IDLE
wantS = (cfg.IRRIG_FILL_MS + cfg.IRRIG_FERT_MS + cfg.IRRIG_SETTLE_MS + ...
         cfg.IRRIG_CHECK_MS + floor(dose_ms(cfg, etcv))) / 1000;
assert(abs(durS - wantS) <= 4 * cfg.STEP_MS / 1000, ...
       '(a) lama siklus %.0f s, seharusnya %.0f s', durS, wantS);
% Resolusi waktu = tick controlTask 2 s. FERT 1126 ms < 2 s -> tepat satu tick
% (44 mL, bukan 25 mL): ini batas fisik, bukan bug. DIST dibulatkan ke atas
% paling banyak satu tick (~2 % volume).
fertMs = st.tLog(3) - st.tLog(2);
distMs = st.tLog(6) - st.tLog(5);
assert(fertMs == cfg.STEP_MS, '(a) FERT %d ms, seharusnya tepat satu tick', fertMs);
assert(abs(distMs - dose_ms(cfg, etcv)) <= cfg.STEP_MS, ...
       '(a) DIST %.0f ms menyimpang dari dosis %.0f ms', distMs, dose_ms(cfg, etcv));
printf('(a) ETc 4,633 -> FILL->FERT->SETTLE->CHECK->DIST (%0.f s), dose 2085 mL  OK\n', durS);
printf('(a2) FERT %d ms (~%0.f mL, minimum satu tick) & DIST %.0f ms (~%0.f mL)  OK\n', ...
       fertMs, fertMs / 1000 * 22.2, distMs, distMs / 1000 * cfg.PUMP_DIST_ML_PER_S);

%% --- (b) permintaan saat siklus berjalan -> tidak ada siklus ganda -------
st = step(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);
assert(st.phase == 0, '(b) siklus baru dimulai padahal anggaran/jeda belum siap');
assert(count_prefix(st, 'irigasi: dose') == 1, '(b) ada siklus kedua');

%% --- (b2) siklus selesai, jeda 12 jam belum lewat -> DITAHAN ------------
st = init_state();
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);
st = run_for(st, cfg, 400000, decide_trigger(true, true, false), etcv, false, tdsOk, true);
assert(st.phase == 0, '(b2) siklus belum selesai');
st = step(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);
assert(st.phase == 0, '(b2) siklus jalan padahal jeda minimum belum lewat');
assert(count_prefix(st, 'jeda minimum belum lewat') >= 1, '(b2) log jeda minimum tidak ada');
assert(strcmp(st.holdReason, 'jeda minimum belum lewat'), ...
       '(b2) alasan tahan ke dashboard bukan "jeda minimum belum lewat"');
assert(near(st.usedMl, 2085, 1), '(b2) volume terpakai berubah padahal siklus ditahan');
printf('(b2) permintaan setelah siklus selesai DITAHAN jeda minimum 12 jam    OK\n');

%% --- (c) setelah jeda minimum lewat -> siklus kedua jalan ---------------
st.now += min_interval_ms(cfg);
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);
assert(st.phase == 1, '(c) siklus kedua tidak jalan setelah jeda minimum');
assert(count_prefix(st, 'irigasi: dose') == 2, '(c) siklus kedua tidak tercatat');
assert(near(st.usedMl, 4170, 2), '(c) volume terpakai bukan 4170 mL');
printf('(c) setelah jeda 12 jam, siklus kedua jalan (terpakai 4170 mL)    OK\n');

%% --- (d) siklus ketiga DITAHAN oleh anggaran harian ---------------------
st = run_for(st, cfg, 400000, true, etcv, false, tdsOk, true);
st = step(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);
assert(count_prefix(st, 'anggaran harian habis') >= 1, '(d) log anggaran habis tidak ada');
assert(strcmp(st.holdReason, 'anggaran harian habis'), ...
       '(d) alasan tahan ke dashboard bukan "anggaran harian habis"');
assert(near(st.usedMl, 4170, 2), '(d) volume terpakai berubah padahal ditahan');
printf('(d) siklus ketiga DITAHAN: anggaran harian 4170/4170 mL           OK\n');

%% --- (e)/(f) ETc turun & naik -> dosis & durasi ikut ---------------------
% Tabel acuan di atas sudah mengunci angka 7,02 / 7,85 / 8,68 / 11,51;
% di sini dipastikan dosis yang dipakai siklus benar-benar nilai itu.
for ci = [7.02, 11.51]
  st2 = init_state();
  st2 = irr_update(st2, cfg, decide_trigger(true, true, false), ci, false, tdsOk, true);
  assert(near(st2.cycleDoseMl, dose_ml(cfg, ci), 1), ...
         '(e/f) dosis siklus ETc %.2f salah', ci);
  assert(st2.cycleDoseMs == floor(dose_ms(cfg, ci)), ...
         '(e/f) durasi siklus ETc %.2f salah', ci);
endfor
printf('(e) ETc 7,02 -> dose 3159 mL, durasi 99,7 s                       OK\n');
printf('(f) ETc 11,51 -> dose 5180 mL, durasi 163,4 s                     OK\n');

%% --- (g) soil invalid -> tidak menyiram --------------------------------
st = init_state();
st = irr_update(st, cfg, decide_trigger(false, true, false), 8.68, false, tdsOk, true);
assert(st.phase == 0, '(g) siklus jalan padahal soil invalid');
assert(st.usedMl == 0, '(g) volume terpakai berubah padahal tidak menyiram');
printf('(g) soil invalid -> tidak menyiram                               OK\n');

%% --- (h) manual override -> siklus boleh jalan -------------------------
st = init_state();
st = irr_update(st, cfg, decide_trigger(false, true, true), 8.68, true, tdsOk, true);
assert(st.phase == 1, '(h) override manual tidak menjalankan siklus');
assert(st.fillOn, '(h) pompa FILL tidak hidup');
assert(isempty(st.holdReason), '(h) alasan tahan tidak dikosongkan saat siklus jalan');
printf('(h) override manual -> siklus jalan (pompa FILL hidup)             OK\n');

%% --- (i) trigger terus TRUE -> tepat satu siklus -----------------------
st = init_state();
st = run_for(st, cfg, 400000, decide_trigger(true, true, false), 8.68, false, tdsOk, true);
assert(count_prefix(st, 'irigasi: dose') == 1, ...
       '(i) permintaan tunggal menghasilkan %d siklus', count_prefix(st, 'irigasi: dose'));
assert(st.phase == 0, '(i) pompa masih jalan setelah siklus selesai');
printf('(i) permintaan terus TRUE: tepat 1 siklus (tidak ada siklus ganda)  OK\n');

%% --- (j) permintaan manual BARU menembus jeda minimum ------------------
st = init_state();
etcv = 8.68;
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);
st = run_for(st, cfg, 400000, true, etcv, false, tdsOk, true);
st = step(st, cfg, decide_trigger(true, true, true), etcv, true, tdsOk, true);   % manual BARU
assert(st.phase == 1, '(j) permintaan manual tidak menembus jeda minimum');
assert(count_prefix(st, 'override manual') == 1, '(j) log override manual tidak ada');
assert(count_prefix(st, 'irigasi: dose') == 2, '(j) siklus manual tidak tercatat');
printf('(j) manual override baru -> menembus jeda minimum, siklus jalan    OK\n');

%% --- (k) override manual TETAP menyala -> tidak ada siklus kedua -------
st = run_for(st, cfg, 400000, true, etcv, true, tdsOk, true);       % siklus manual selesai
assert(st.phase == 0, '(k) siklus belum selesai');
st = run_for(st, cfg, 120000, decide_trigger(true, true, true), etcv, true, tdsOk, true);
assert(count_prefix(st, 'irigasi: dose') == 2, ...
       '(k) override manual menghasilkan %d siklus (pompa menyala terus-menerus)', ...
       count_prefix(st, 'irigasi: dose'));
assert(st.phase == 0, '(k) pompa menyala terus-menerus');
printf('(k) override manual tetap menyala: tepat 1 siklus manual          OK\n');

%% --- (l) override dilepas lalu dinyalakan lagi -> siklus baru boleh ----
st = step(st, cfg, decide_trigger(true, true, false), etcv, false, tdsOk, true);   % dilepas
st = step(st, cfg, decide_trigger(true, true, true), etcv, true, tdsOk, true);     % nyala lagi
assert(st.phase == 1, '(l) permintaan manual baru tidak menjalankan siklus');
assert(count_prefix(st, 'irigasi: dose') == 3, '(l) siklus manual baru tidak tercatat');
printf('(l) manual dilepas lalu dinyalakan lagi -> siklus baru jalan      OK\n');

%% --- (m) TDS dalam rentang -> DIST jalan ------------------------------
st = init_state();
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, 1000, true);
st = run_for(st, cfg, 400000, true, etcv, false, 1000, true);
assert(phase_seq(st, {'FILL', 'FERT', 'SETTLE', 'CHECK', 'DIST', 'IDLE'}), ...
       '(m) siklus tidak sampai DIST walau TDS dalam rentang');
assert(near(st.usedMl, dose_ml(cfg, etcv), 1), '(m) anggaran tidak terpakai');
printf('(m) TDS 1000 ppm (dalam rentang) -> DIST jalan                    OK\n');

%% --- (n) TDS TERBACA di luar rentang -> DIST DITAHAN -------------------
% Kimia jelas salah: jangan siram. Anggaran dikembalikan karena air tidak
% pernah sampai ke tanaman.
for rk = [1500, 700]
  st = init_state();
  st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, rk, true);
  st = run_for(st, cfg, 400000, true, etcv, false, rk, true);
  assert(st.phase == 0, '(n) siklus TDS %d ppm tidak berhenti di IDLE', rk);
  assert(!any(strcmp(st.phaseLog, 'DIST')), ...
         '(n) DIST jalan padahal TDS %d ppm di luar rentang', rk);
  assert(!st.distOn, '(n) pompa DIST masih menyala');
  assert(!any(st.snap(:,1) == 5), '(n) fase DIST pernah masuk pada TDS %d ppm', rk);
  assert(near(st.usedMl, 0, 0.5), '(n) anggaran tidak dikembalikan (%.0f mL)', st.usedMl);
  assert(strcmp(st.holdReason, 'TDS di luar rentang, distribusi ditahan'), ...
         '(n) alasan tahan ke dashboard salah: "%s"', st.holdReason);
  assert(count_prefix(st, 'siklus dibatalkan (TDS)') == 1, '(n) log pembatalan tidak ada');
endfor
printf('(n) TDS 1500 / 700 ppm (terbaca, di luar rentang) -> DIST DITAHAN  OK\n');

%% --- (o) TDS tidak terbaca -> DIST tetap jalan + peringatan ------------
st = init_state();
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, NaN, false);
st = run_for(st, cfg, 400000, true, etcv, false, NaN, false);
assert(phase_seq(st, {'FILL', 'FERT', 'SETTLE', 'CHECK', 'DIST', 'IDLE'}), ...
       '(o) probe tidak terbaca malah menahan air (tanaman tidak disiram)');
assert(near(st.usedMl, dose_ml(cfg, etcv), 1), '(o) anggaran tidak terpakai');
assert(count_prefix(st, 'TDS tidak terbaca: air tetap didistribusikan') >= 1, ...
       '(o) peringatan TDS tidak terbaca tidak dicatat');
printf('(o) TDS tidak terbaca (NaN) -> DIST tetap jalan + peringatan      OK\n');

%% --- (p) setelah ditahan TDS: tidak ada FILL berulang, manual bisa ulang
st = init_state();
st = irr_update(st, cfg, decide_trigger(true, true, false), etcv, false, 1500, true);
st = run_for(st, cfg, 400000, true, etcv, false, 1500, true);     % siklus ditahan
nFill = sum(strcmp(st.phaseLog, 'FILL'));
assert(nFill == 1, '(p) FILL terjadi %d kali', nFill);
st = run_for(st, cfg, 600000, true, etcv, false, 1500, true);     % trigger tetap TRUE 10 menit
assert(sum(strcmp(st.phaseLog, 'FILL')) == 1, ...
       '(p) FILL berulang tiap siklus walau tidak ada yang dialirkan');
assert(strcmp(st.holdReason, 'TDS di luar rentang, distribusi ditahan'), ...
       '(p) alasan TDS tertimpa alasan lain di dashboard: "%s"', st.holdReason);
% Operator memperbaiki dosis lalu menyalakan sakelar manual: boleh coba lagi.
st = step(st, cfg, decide_trigger(true, true, true), etcv, true, 1000, true);   % manual BARU
assert(strcmp(st.phaseLog{end}, 'FILL'), '(p) permintaan manual tidak memulai siklus');
st = run_for(st, cfg, 400000, true, etcv, true, 1000, true);
assert(any(strcmp(st.phaseLog, 'DIST')), '(p) siklus manual tidak sampai DIST');
assert(near(st.usedMl, dose_ml(cfg, etcv), 1), '(p) anggaran siklus manual salah');
printf('(p) ditahan TDS: tanpa FILL berulang, manual baru bisa coba lagi  OK\n');

printf('\nSEMUA SKENARIO STATE MACHINE FERTIGASI LOLOS\n');
endfunction
