% fuzzy_sim.m — replika mesin fuzzy firmware untuk simulasi duty cycle blower
%
% Jalankan (dari akar repo):
%   octave --no-gui --eval "addpath('firmware/tools'); fuzzy_sim"
%
% Plot butuh graphics toolkit; untuk headless pasang gnuplot
% (sudo pacman -S gnuplot). Tanpa itu tabel dan .fis tetap dibuat.
%
% Isi fungsi ini adalah SALINAN dari firmware/src/fuzzy.cpp (versi 1.3). Kalau
% tabel membership atau tabel rule di fuzzy.cpp berubah, salinan di sini wajib
% ikut berubah — tiap blok diberi nomor baris sumbernya.
%
% Catatan kejujuran: firmware memakai FUZZY SUGENO-0 (rata-rata berbobot dengan
% consequent singleton), bukan Mamdani murni. Rumusnya sama dengan
% defuzzifikasi centroid yang tertulis di proposal, dan .fis yang dihasilkan
% memakai consequent segitiga agar bisa dibuka di MATLAB/Octave (selisih < 1 %).
%
% Keluaran:
%   firmware/tools/out/*.png            grafik
%   firmware/tools/fuzzy_rules.fis      model untuk MATLAB fuzzyLogicDesigner

function fuzzy_sim()

% ---- Fungsi pembantu (nested: butuh lingkup variabel di atas) ----

function y = trimf(x, a, b, c)
  % fuzzy.cpp:3-7
  y = zeros(size(x));
  up = (x > a) & (x <= b);
  dn = (x > b) & (x < c);
  y(up) = (x(up) - a) / (b - a);
  y(dn) = (c - x(dn)) / (c - b);
endfunction

function m = membership(temp, hum, soil, vpd, etc)
  % urutan kolom harus sama dengan fuzzy.cpp:84-90
  v = [temp hum soil vpd etc];
  m = zeros(numel(MU_NAME), 1);
  pos = 1;
  for k = 1:numel(v)
    for j = 1:rows(IN_mf{k})
      p = IN_mf{k}(j,:);
      m(pos) = trimf(v(k), p(1), p(2), p(3));
      pos++;
    end
  end
endfunction

function lvl = eval_rules(rules, m)
  % fuzzy.cpp:73-81 — rata-rata berbobot; level 0 hanya mengencerkan
  num = 0; den = 0;
  for ri = 1:rows(rules)
    w = min(m(rules{ri,2}), m(rules{ri,3}));
    num += w * rules{ri,4};
    den += w;
  end
  if den > 0, lvl = num / den; else lvl = 0; end
endfunction

function o = fuzzy_eval(temp, hum, soil, vpd, etc)
  m = membership(temp, hum, soil, vpd, etc);
  o.mist     = eval_rules(MIST_RULES, m);
  o.blower   = eval_rules(BLOWER_RULES, m);
  o.irrig    = eval_rules(IRRIG_RULES, m);
  o.blowerOn = o.blower > ON_THRESHOLD;   % fuzzy.cpp:104
  o.mistOn   = o.mist > 0.1;              % fuzzy.cpp:103
  o.irrigOn  = o.irrig > 0.1;             % fuzzy.cpp:106
endfunction

function d = duty_cmd(o)
  % actuator.cpp:7-20 — blowerLevel di bawah BLOWER_MIN_DUTY dibulatkan naik
  if ~o.blowerOn
    d = 0;
  else
    d = max(o.blower, HW_START);
  end
endfunction

function s = state_label(o)
  if o.blowerOn && o.blower < HW_START
    s = sprintf("(ON, level %.0f%% dibulatkan ke %.0f%%)", 100*o.blower, 100*HW_START);
  elseif o.blowerOn
    s = "ON";
  else
    s = "OFF";
  end
endfunction

function ok = do_plots()
  % Plot butuh display; gnuplot bisa jalan tanpa X.
  kt = available_graphics_toolkits();
  for i = 1:numel(kt)
    try
      graphics_toolkit(kt{i});
      f = figure("visible", "off"); close(f);
      ok = true; return;
    catch
    end_try_catch
  end
  ok = false;
endfunction

function write_fis_file(path)
  % Model Mamdani 3 keluaran dengan consequent segitiga berlebar sama. Untuk
  % segitiga yang lebarnya sama, centroid hasil max-aggregation sama dengan
  % rata-rata berbobot di firmware, jadi .fis ini mencerminkan logika yang
  % dijalankan perangkat. Level 0 ditulis sebagai segitiga kecil
  % (puncak 0,03) karena .fis tidak menerima singleton tepat di titik 0.
  lvl_out  = {"misting", "blower", "irigasi"};
  rules    = {MIST_RULES, BLOWER_RULES, IRRIG_RULES};
  centers  = {[0 0.3 0.6 1.0], [0 0.4 0.7 1.0], [0 0.5 1.0]};
  onames   = {{'Off', 'Low', 'Med', 'High'}, {'Off', 'Low', 'Med', 'High'}, ...
              {'Off', 'Low', 'High'}};
  halfw = 0.15;

  fid = fopen(path, "w");
  fprintf(fid, "[General]\nversion=2.0\ntype=mamdani\nname=fuzzy_ctrl_esp32\n");
  fprintf(fid, "method=min\nlocation=0,0\nopts=CNF,3,1,3,0,0,0\n\n");

  for rk = 1:numel(IN_name)
    fprintf(fid, "[Input%d]\nname=%s\nrange=[%g %g]\nNumMFs=%d\n", ...
            rk, IN_name{rk}, IN_range{rk}(1), IN_range{rk}(2), rows(IN_mf{rk}));
    for rj = 1:rows(IN_mf{rk})
      p = IN_mf{rk}(rj,:);
      nm = IN_mfname{rk}{rj};
      fprintf(fid, "MF%d=%s : trimf : [%g %g %g]\n", rj, nm, p(1), p(2), p(3));
    end
    fprintf(fid, "\n");
  end

  for rk = 1:3
    fprintf(fid, "[Output%d]\nname=%s\nrange=[0 1]\nNumMFs=%d\n", ...
            rk, lvl_out{rk}, numel(centers{rk}));
    ons = onames{rk};
    for rj = 1:numel(centers{rk})
      c = centers{rk}(rj);
      if c == 0
        fprintf(fid, "MF%d=%s : trimf : [0 0.015 0.03]\n", rj, ons{rj});
      else
        fprintf(fid, "MF%d=%s : trimf : [%g %g %g]\n", rj, ons{rj}, ...
                max(0, c - halfw), c, min(1, c + halfw));
      end
    end
    fprintf(fid, "\n");
  end

  for rk = 1:3
    fprintf(fid, "[Rules%d]\n", rk);
    for ri = 1:rows(rules{rk})
      r = rules{rk}(ri,:);
      c = r{4};
      ons = onames{rk};
      if c == 0
        cmf = "Off";
      else
        idx = find(centers{rk} == c, 1);
        cmf = ons{idx};
      end
      fprintf(fid, "%d %d (%s) : %d %d (%s) -> %d (%s)\n", ...
              r{2}, r{2}, MU_NAME{r{2}}, r{3}, r{3}, MU_NAME{r{3}}, rk, cmf);
    end
    fprintf(fid, "\n");
  end
  fclose(fid);
endfunction


%% --- Fungsi pembantu (Octave tidak meng-hoist fungsi di berkas skrip) ----
% Urutan mu[] harus sama dengan enum di fuzzy.cpp:37 dan isi fuzzy.cpp:84-90
%   1 TC 2 TO 3 TH 4 HL 5 HO 6 HH 7 SD 8 SO 9 SW 10 VL 11 VH 12 EL 13 EH
MU_NAME = {"TC","TO","TH","HL","HO","HH","SD","SO","SW","VL","VH","EL","EH"};

% Setiap masukan: nama untuk .fis, label plot, rentang, dan membership function
% (a, b, c) = trapesium fuzzy.cpp:9-29
IN_name   = {'Temperature', 'Humidity', 'Soil', 'VPD', 'ETc'};
IN_label  = {'Suhu (°C)', 'Kelembapan udara (%)', 'Kelembapan tanah (%)', ...
             'VPD (kPa)', 'ETc (mm)'};
IN_range  = {[0 50], [0 100], [0 100], [0 3], [0 14]};
IN_mfname = {{'Cold', 'Opt', 'Hot'}, {'Low', 'Opt', 'High'}, ...
             {'Dry', 'Opt', 'Wet'}, {'Low', 'High'}, {'Low', 'High'}};
IN_mf     = {[10 18 25; 22 27 32; 28 34 42], ...
             [0 30 55; 45 65 80; 70 85 100], ...
             [0 15 35; 30 50 70; 60 80 100], ...
             [0 0.2 0.5; 1.3 1.6 2.0], ...
             [0 2 5; 4 6 12]};

%% --- Tabel rule (fuzzy.cpp:41-74), level = consequent singleton ---------
% tiap baris: {nama, anteseden A, anteseden B, level}, indeks = posisi di MU_NAME
% A dan B boleh sama; min(mu,mu)=mu, jadi aturan itu jadi pengencer (level 0)
MIST_RULES = {
  "H+S_kering",  3,  7, 1.0     % {T_HOT, S_DRY, MIST_HIGH}
  "H+S_optimal", 3,  8, 0.6     % {T_HOT, S_OPT, MIST_MED}
  "H+S_basah",   3,  9, 0.3     % {T_HOT, S_WET, MIST_LOW}
  "H+V_tinggi",  3, 11, 1.0     % {T_HOT, V_HIGH, MIST_HIGH}
  "O+S_kering",  2,  7, 0.6     % {T_OPT, S_DRY, MIST_MED}
  "O+V_tinggi",  2, 11, 0.6     % {T_OPT, V_HIGH, MIST_MED}
  "Dingin",      1,  1, 0.0     % {T_COLD, T_COLD, 0.0}
  "O+S_basah",   2,  9, 0.0     % {T_OPT, S_WET, 0.0}
  "Udara_jenuh",10, 10, 0.0     % {V_LOW, V_LOW, 0.0}
};
BLOWER_RULES = {
  "H+RH_tinggi", 3,  6, 1.0     % {T_HOT, H_HIGH, BLOWER_HIGH}
  "O+RH_tinggi", 2,  6, 0.7     % {T_OPT, H_HIGH, BLOWER_MED}
  "H+RH_rendah", 3,  4, 0.4     % {T_HOT, H_LOW, BLOWER_LOW}
  "H+RH_mid",    3,  5, 0.7     % {T_HOT, H_OPT, BLOWER_MED}  % pengisi zona RH 45-80
  "O+RH_mid",    2,  5, 0.4     % {T_OPT, H_OPT, BLOWER_LOW}  % pengisi zona RH 45-80
  "Udara_jenuh",10, 10, 0.4     % {V_LOW, V_LOW, BLOWER_LOW}
  "Dingin",      1,  1, 0.0     % {T_COLD, T_COLD, 0.0}
};
IRRIG_RULES = {
  "Kering+ET_tinggi",  7, 13, 1.0   % {S_DRY, E_HIGH, IRR_HIGH}
  "Kering+ET_rendah",  7, 12, 0.5   % {S_DRY, E_LOW, IRR_LOW}
  "Optimal+ET_tinggi", 8, 13, 0.5   % {S_OPT, E_HIGH, IRR_LOW}
  "Optimal+ET_rendah", 8, 12, 0.0   % {S_OPT, E_LOW, 0.0}
  "Basah",             9,  9, 0.0   % {S_WET, S_WET, 0.0}
};

% ambang nyalakan firmware (fuzzy.cpp:103-106) dan hasil uji perangkat keras
ON_THRESHOLD = 0.10;      % fuzzy.cpp: blowerOn = blowerLevel > 0.1
HW_START     = 0.20;      % kipas baru berputar dari duty 20 % (hw_test, Okt 2026)
HW_STOP      = 0.13;      % kipas berhenti di duty ~13 %
LEDC_MAX     = 255;       % 8 bit, BLOWER_RES (config.h:39)
PWM_FREQ     = 1000;      % BLOWER_FREQ (config.h:38)

%% --- Kasus acuan (dihitung dari kode yang sama) --------------------------
cases = [30 70 35 1.6 5.5;    % kasus yang sama dengan contoh proposal 2.2.4
         32 85 50 1.8 6.0;
         34 30 20 2.0 4.0;
         25 75 55 0.6 2.0;
         28 72 45 1.5 5.0;
         18 50 40 0.3 1.0;    % pita mati: 0,13 < duty < 0,20
         22 60 15 1.0 11.0;
         30 70 80 1.6 2.0];
expect = [0.7356 0.5364 0.5000;
          0.7714 1.0000 0.5000;
          1.0000 0.4000 0.5000;
          0.0000 0.5500 0.0000;
          0.6000 0.4600 0.5000;
          0.0000 0.1600 0.0000;
          0.0000 0.0000 1.0000;
          0.4591 0.5364 0.0000];

fprintf("== HASIL SIMULASI ==\n");
fprintf("%-24s %7s %7s %7s   %s\n", "T,RH,soil,VPD,ETc", "mist", "blower", "irig", "duty blower");
got = zeros(rows(cases), 3);
for ci = 1:rows(cases)
  o = fuzzy_eval(cases(ci,1), cases(ci,2), cases(ci,3), cases(ci,4), cases(ci,5));
  got(ci,:) = [o.mist, o.blower, o.irrig];
  fprintf("%-24s %7.4f %7.4f %7.4f   %3d/255 = %3.0f %% %s\n", ...
          mat2str(cases(ci,:)), o.mist, o.blower, o.irrig, ...
          round(duty_cmd(o)*LEDC_MAX), 100*duty_cmd(o), state_label(o));
end
err = max(max(abs(got - expect)));
fprintf("galat maksimum terhadap nilai acuan: %.2e\n", err);
assert(err < 5e-4, "nilai acuan tidak cocok, tabel rule mungkin berubah");
printf("SEMUA NILAI ACUAN COCOK\n\n");

snapped = find(got(:,2) > ON_THRESHOLD & got(:,2) < HW_START);
if numel(snapped) > 0
  fprintf("DIBULATKAN: %d dari %d kasus punya level di bawah duty minimum (%.0f%%),\n", ...
          numel(snapped), rows(cases), 100*HW_START);
  fprintf("             actuator.cpp menaikkan level itu ke %.0f%% supaya kipas\n", ...
          100*HW_START);
  fprintf("             benar-benar berputar. Duty di kolom kiri sudah nilai\n");
  fprintf("             yang benar-benar dikirim ke LEDC.\n\n");
end

%% --- Sapuan duty blower vs kelembapan (tanah 50 %, VPD 1,5 kPa) --------
fprintf("\n-- sapuan duty blower (soil 50, VPD 1.5) --\n");
fprintf("%6s %10s %10s %12s %12s\n", "RH", "lvl30", "lvl34", "duty30", "duty34");
for rh = [10 20 30 40 50 55 58 60 65 70 72 75 80 90]
  o30 = fuzzy_eval(30, rh, 50, 1.5, 5.0);
  o34 = fuzzy_eval(34, rh, 50, 1.5, 5.0);
  fprintf("%6.0f %10.3f %10.3f %12.3f %12.3f\n", rh, ...
          o30.blower, o34.blower, duty_cmd(o30), duty_cmd(o34));
end

%% --- Tulis model .fis (selalu, walau plot dilewati) --------------------
write_fis_file("firmware/tools/fuzzy_rules.fis");

%% --- Plot: butuh graphics toolkit (pasang gnuplot untuk headless) -------
if do_plots()

  % Plot 1: kurva membership
  fig = figure("visible", "off", "position", [0 0 1500 380]);
  for k = 1:5
    x = linspace(IN_range{k}(1), IN_range{k}(2), 400);
    subplot(1, 5, k); hold on;
    for j = 1:rows(IN_mf{k})
      pp = IN_mf{k}(j,:);
      plot(x, trimf(x, pp(1), pp(2), pp(3)), "linewidth", 1.6);
    end
    title(IN_label{k}); grid on; ylim([0 1.05]);
    if k == 1, legend(IN_mfname{k}{:}, "location", "northwest"); endif
  end
  print(fig, "-dpng", "-r110", "firmware/tools/out/01_membership.png");
  close(fig);

  % Plot 2: peta panas level blower (suhu x kelembapan), VPD tetap
  % Sapuan RH dibatasi 95 %: di atas 100 % seluruh keanggotaan RH bernilai nol,
  % jadi tidak ada aturan yang bisa aktif (fan tidak mungkin menyala di RH 100 %).
  Tv = linspace(16, 40, 120);
  Rv = linspace(10, 95, 120);
  [TT, RR] = meshgrid(Tv, Rv);
  Bv = zeros(size(TT));
  for i = 1:numel(TT)
    Bv(i) = fuzzy_eval(TT(i), RR(i), 50, 1.5, 5.0).blower;
  end

  % Plot 3: peta panas level blower (suhu x VPD), RH tetap
  Vv = linspace(0, 2.5, 120);
  [TT2, VV] = meshgrid(Tv, Vv);
  B2 = zeros(size(TT2));
  for i = 1:numel(TT2)
    B2(i) = fuzzy_eval(TT2(i), 60, 50, VV(i), 5.0).blower;
  end

  fig = figure("visible", "off", "position", [0 0 1150 470]);
  subplot(1, 2, 1);
  contourf(TT, RR, Bv, [0 0.1 0.4 0.6 0.8 1.0], "LineStyle", "none");
  hold on; contour(TT, RR, Bv, [0.4 0.7 1.0], "k", "linewidth", 0.9);
  colormap(jet); colorbar; grid on;
  xlabel("Suhu (°C)"); ylabel("Kelembapan udara (%)");
  title("Level blower, VPD = 1,5 kPa");
  subplot(1, 2, 2);
  contourf(TT2, VV, B2, [0 0.1 0.4 0.6 0.8 1.0], "LineStyle", "none");
  hold on; contour(TT2, VV, B2, [0.4 0.7 1.0], "k", "linewidth", 0.9);
  colormap(jet); colorbar; grid on;
  xlabel("Suhu (°C)"); ylabel("VPD (kPa)");
  title("Level blower, RH = 60 %");
  print(fig, "-dpng", "-r110", "firmware/tools/out/02_peta_blower.png");
  close(fig);

  % Plot 4: sapuan duty vs kelembapan (bukti transisi bertahap)
  fig = figure("visible", "off", "position", [0 0 1100 500]);
  for T = [30 34]
    lv = arrayfun(@(rh) duty_cmd(fuzzy_eval(T, rh, 50, 1.5, 5.0)), Rv);
    plot(Rv, 100*lv, "linewidth", 1.8); hold on;
  end
  plot([0 max(Rv)], [100*ON_THRESHOLD 100*ON_THRESHOLD], "--", "color", [0 .6 0]);
  plot([0 max(Rv)], [100*HW_START 100*HW_START], ":", "color", [0.8 0 0]);
  plot([0 max(Rv)], [100*HW_STOP 100*HW_STOP], ":", "color", [0.8 0 0]);
  text(1, 100*HW_START+3, "duty minimum kipas 20 % (dibulatkan naik)", "color", [0.8 0 0]);
  text(1, 100*ON_THRESHOLD+3, "ambang logika fuzzy 10 %", "color", [0 .6 0]);
  ylim([-5 108]); grid on;
  xlabel("Kelembapan udara (%)"); ylabel("Duty cycle blower (%)");
  title("Duty blower vs kelembapan udara (tanah 50 %, VPD 1,5 kPa)");
  legend("T = 30 °C", "T = 34 °C", "location", "southeast");
  print(fig, "-dpng", "-r110", "firmware/tools/out/03_sapuan_duty.png");
  close(fig);

  % Plot 5: level misting dan irigasi vs kelembapan tanah
  Sv = linspace(0, 100, 200);
  m1 = arrayfun(@(s) fuzzy_eval(30, 65, s, 1.6, 5.5).mist, Sv);
  m2 = arrayfun(@(s) fuzzy_eval(30, 65, s, 1.6, 8.0).mist, Sv);
  i1 = arrayfun(@(s) fuzzy_eval(30, 65, s, 1.6, 5.5).irrig, Sv);
  i2 = arrayfun(@(s) fuzzy_eval(30, 65, s, 1.6, 8.0).irrig, Sv);
  fig = figure("visible", "off", "position", [0 0 1150 470]);
  subplot(1, 2, 1);
  plot(Sv, 100*m1, "-", Sv, 100*m2, "--", "linewidth", 1.8);
  ylim([-5 108]); grid on;
  xlabel("Kelembapan tanah (%)"); ylabel("Level (%)");
  title("Misting (T 30 °C, RH 65 %, VPD 1,6 kPa)");
  legend("ETc 5,5 mm", "ETc 8,0 mm", "location", "northeast");
  subplot(1, 2, 2);
  plot(Sv, 100*i1, "-", Sv, 100*i2, "--", "linewidth", 1.8);
  ylim([-5 108]); grid on;
  xlabel("Kelembapan tanah (%)"); ylabel("Level (%)");
  title("Irigasi, masukan yang sama");
  legend("ETc 5,5 mm", "ETc 8,0 mm", "location", "northeast");
  print(fig, "-dpng", "-r110", "firmware/tools/out/04_misting_irigasi.png");
  close(fig);

else
  printf('Plot dilewati: belum ada graphics toolkit yang bisa dipakai.\n');
  printf('Headless: sudo pacman -S gnuplot, lalu jalankan ulang.\n');
endif

fprintf("\nPWM perangkat: %d Hz, %d tingkat (duty = level x %d)\n", ...
        PWM_FREQ, LEDC_MAX+1, LEDC_MAX);
printf("Model  : firmware/tools/fuzzy_rules.fis\n");
if do_plots(), printf("Grafik : firmware/tools/out/\n"); endif

endfunction

