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
% Skenario yang diuji:
%   (a) sensor invalid -> abort -> sensor pulih dengan permintaan masih TRUE
%       -> siklus LANGSUNG jalan lagi (bug prevTrigger yang dulu bikin
%          irigasi mati permanen)
%   (b) permintaan turun lalu naik lagi -> siklus baru (satu permintaan =
%       satu siklus)
%   (c) abort di tengah fase FERT -> semua pompa mati
%   (d) permintaan TRUE terus-menerus -> hanya SATU siklus
%   (e) satu sampel TDS invalid tidak membatalkan fase FERT; tiga sampel
%       berturut-turut baru membatalkan (fail-safe + toleransi noise)

function irrig_sim()

%% --- Parameter (salinan config.h) ----------------------------------------
cfg.IRRIG_FILL_MS           = 30000;
cfg.IRRIG_DIST_MS           = 120000;
cfg.IRRIG_FERT_TIMEOUT_MS   = 120000;
cfg.IRRIG_FERT_PULSE_MS     = 2000;
cfg.IRRIG_FERT_IDLE_MS      = 1000;
cfg.IRRIG_FERT_INVALID_LIMIT = 3;
cfg.TDS_TARGET_PPM          = 1000;
cfg.IRRIG_TDS_HYSTERESIS    = 20;
cfg.STEP_MS                 = 2000;    % periode controlTask

%% --- Fungsi pembantu (nested: Octave tidak meng-hoist fungsi skrip) ------
function st = next_phase(st, p)
  st.phase = p;
  st.phaseStart = st.now;
  st.fertInvalid = 0;
  st.pumps = [0 0 0 0];
endfunction

function st = irr_update(st, trigger, tdsValid, tdsPpm)
  % Edge-detect: 0->1 memulai siklus; permintaan yang terus tinggi tidak
  % memicu siklus kedua.
  if trigger && !st.prevTrigger && st.phase == 0
    if st.resumePending
      st.log{end+1} = 'siklus DIPULIHKAN';
      st.resumePending = false;
    else
      st.log{end+1} = 'siklus dimulai';
    endif
    st = next_phase(st, 1);        % FILL
  endif
  st.prevTrigger = trigger;
  if !trigger
    st.resumePending = false;
  endif

  if st.phase == 1                            % FILL
    st.pumps = [1 0 0 0];
    if st.now - st.phaseStart >= cfg.IRRIG_FILL_MS
      st = next_phase(st, 2);                  % FERT
    endif
  elseif st.phase == 2                        % FERT
    fertCancelled = false;
    if !tdsValid
      st.fertInvalid++;
      if st.fertInvalid >= cfg.IRRIG_FERT_INVALID_LIMIT
        st.log{end+1} = 'FERT batal (TDS invalid)';
        st = next_phase(st, 3);                % DIST
        fertCancelled = true;
      endif
    else
      st.fertInvalid = 0;
    endif
    if !fertCancelled
      t = st.now - st.phaseStart;
      fertOn = mod(t, cfg.IRRIG_FERT_PULSE_MS + cfg.IRRIG_FERT_IDLE_MS) < cfg.IRRIG_FERT_PULSE_MS;
      st.pumps = [0 fertOn fertOn 0];
      if t >= cfg.IRRIG_FERT_TIMEOUT_MS
        st = next_phase(st, 3);
      elseif tdsValid && !fertOn && tdsPpm >= cfg.TDS_TARGET_PPM - cfg.IRRIG_TDS_HYSTERESIS
        st.log{end+1} = 'TDS target tercapai';
        st = next_phase(st, 3);
      endif
    endif
  elseif st.phase == 3                        % DIST
    st.pumps = [0 0 0 1];
    if st.now - st.phaseStart >= cfg.IRRIG_DIST_MS
      st.log{end+1} = 'siklus selesai';
      st = next_phase(st, 0);
    endif
  endif
endfunction

function st = irr_abort(st)
  if st.prevTrigger
    st.resumePending = true;
  endif
  st.prevTrigger = false;
  if st.phase != 0
    st.log{end+1} = 'siklus dibatalkan (abort)';
  endif
  st = next_phase(st, 0);
endfunction

function st = init_state()
  st.phase = 0;                  % 0 IDLE, 1 FILL, 2 FERT, 3 DIST
  st.phaseStart = 0;
  st.now = 0;
  st.prevTrigger = false;
  st.resumePending = false;
  st.fertInvalid = 0;
  st.pumps = [0 0 0 0];
  st.log = {};
endfunction

function n = count_log(st, what)
  n = sum(strcmp(st.log, what));
endfunction

%% --- (a) abort saat sensor invalid, lalu sensor pulih ---------------------
st = init_state();
st = irr_update(st, true, true, 400);        % permintaan naik -> siklus mulai
assert(st.phase == 1, '(a) siklus tidak dimulai');
st.now += cfg.STEP_MS;
st = irr_abort(st);                          % sensor tidak valid -> abort
assert(st.phase == 0, '(a) abort tidak mengembalikan fase ke IDLE');
assert(all(st.pumps == 0), '(a) abort tidak mematikan semua pompa');
st.now += cfg.STEP_MS;
st = irr_update(st, true, true, 400);        % sensor pulih, permintaan masih TRUE
assert(st.phase == 1, '(a) siklus tidak berjalan lagi setelah pemulihan');
assert(count_log(st, 'siklus DIPULIHKAN') == 1, '(a) log "dipulihkan" tidak ada');
printf('(a) abort -> pemulihan: siklus jalan lagi, log "DIPULIHKAN" ada  OK\n');

%% --- (b) permintaan turun lalu naik lagi -> siklus baru -------------------
st = init_state();
st = irr_update(st, true, true, 400);        % siklus #1
while st.phase != 0                          % biarkan siklus #1 selesai
  st.now += cfg.STEP_MS;
  st = irr_update(st, true, true, 400);
endwhile
assert(count_log(st, 'siklus dimulai') == 1, '(b) lebih dari satu siklus dari satu permintaan');
st.now += cfg.STEP_MS;
st = irr_update(st, false, true, 400);       % permintaan turun (sakelar manual mati)
assert(st.phase == 0, '(b) fase berubah tanpa permintaan');
st.now += cfg.STEP_MS;
st = irr_update(st, true, true, 400);        % permintaan naik lagi -> siklus #2
assert(st.phase == 1, '(b) permintaan baru tidak memicu siklus');
assert(count_log(st, 'siklus dimulai') == 2, '(b) siklus baru tidak tercatat');
printf('(b) permintaan turun -> naik: siklus baru jalan, 2 siklus total  OK\n');

%% --- (c) abort di tengah fase FERT -> semua pompa mati ------------------
st = init_state();
st = irr_update(st, true, true, 400);
while st.phase != 2                          % majukan ke fase FERT
  st.now += cfg.STEP_MS;
  st = irr_update(st, true, true, 400);
endwhile
% Pompa FERT berdenyut (2 s nyala, 1 s jeda) -> cek ada saat pompa hidup
% di dalam fase FERT, bukan pada satu iterasi tertentu.
fertSeen = false;
for k = 1:6
  st.now += cfg.STEP_MS;
  st = irr_update(st, true, true, 400);
  if st.phase == 2 && any(st.pumps(2:3))
    fertSeen = true;
  endif
endfor
assert(fertSeen, '(c) pompa FERT tidak pernah nyala di fase FERT');
st.now += cfg.STEP_MS;
st = irr_abort(st);
assert(st.phase == 0, '(c) abort di tengah FERT tidak mengembalikan fase ke IDLE');
assert(all(st.pumps == 0), '(c) abort di tengah FERT tidak mematikan semua pompa');
printf('(c) abort di tengah FERT: semua pompa mati                    OK\n');

%% --- (d) permintaan TRUE terus-menerus -> hanya satu siklus -------------
st = init_state();
for k = 1:200                                 % 200 x 2 s = 400 s
  st.now += cfg.STEP_MS;
  st = irr_update(st, true, true, 400);
endfor
assert(count_log(st, 'siklus dimulai') == 1, ...
       '(d) permintaan tunggal menghasilkan %d siklus', count_log(st, 'siklus dimulai'));
printf('(d) permintaan terus TRUE: tepat 1 siklus dalam 400 s          OK\n');

%% --- (e) toleransi noise TDS di fase FERT -------------------------------
st = init_state();
st = irr_update(st, true, true, 400);
while st.phase != 2
  st.now += cfg.STEP_MS;
  st = irr_update(st, true, true, 400);
endwhile
st.now += cfg.STEP_MS;
st = irr_update(st, true, false, 0);         % sampel invalid #1
assert(st.phase == 2, '(e) 1 sampel TDS invalid membatalkan fase FERT');
st.now += cfg.STEP_MS;
st = irr_update(st, true, false, 0);         % sampel invalid #2
assert(st.phase == 2, '(e) 2 sampel TDS invalid membatalkan fase FERT');
st.now += cfg.STEP_MS;
st = irr_update(st, true, false, 0);         % sampel invalid #3
assert(st.phase == 3, '(e) 3 sampel TDS invalid tidak membatalkan fase FERT');
assert(count_log(st, 'FERT batal (TDS invalid)') == 1, '(e) log pembatalan FERT hilang');
printf('(e) 1-2 sampel TDS invalid tolerated, 3 berturut membatalkan    OK\n');

printf('\nSEMUA SKENARIO STATE MACHINE IRIGASI LOLOS\n');
endfunction
