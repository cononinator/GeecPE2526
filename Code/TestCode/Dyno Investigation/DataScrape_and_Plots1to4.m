clear all

%% Note
% all 28 txt files and excel file must be be stored in the same directory

%% Gather Data
%read in data from excel table
data = readtable('Dyno Test 2 Plan.xlsx','Sheet','Test Data', 'Range', 'A2', VariableNamingRule='preserve');

% Convert entire table to array
data_array = table2array(data(:, 1:11));
% Extract individual columns

millis_start_arr = data_array(:, 1);
millis_end_arr = data_array(:, 2);
run_num_arr = data_array(:, 3);
throttle_arr = data_array(:, 4);
current_arr = data_array(:, 5);
speed_arr = data_array(:, 6);
torque_arr = data_array(:, 7);
PE_accel_start= data_array(:, 8);
PE_accel_end= data_array(:, 9);
PE_ss_start= data_array(:, 10);
PE_ss_end= data_array(:, 11);
% Get the File column (column 12) from the original table
file_names = data.File;

% Preallocate structure to store PE data for all runs
PE_runs = struct();

% Loop through ALL runs 
for i = 1:length(run_num_arr)
    ss_begin = PE_ss_start(i);      % Row where steady state begins
    ss_finish = PE_ss_end(i);       % Row where steady state finishes
    Filename = data.File{i};        % Get relevant file name from Excel
    
    fprintf('Processing Run %d: %s (lines %d to %d)\n', i, Filename, ss_begin, ss_finish);
    
    PE_data = readmatrix(Filename); % Read the txt file. There are 28 different txt files for the expt :(
    
    % Extract steady state section
    ss_data = PE_data(ss_begin:ss_finish, :);
    
    % Store data for this run in structure
    PE_runs(i).run_number = run_num_arr(i);
    PE_runs(i).throttle = throttle_arr(i);
    PE_runs(i).filename = Filename;

    % take data from the txt tiles
    PE_runs(i).ss_millis = ss_data(:, 1);
    PE_runs(i).ss_battery_current = ss_data(:, 2);
    PE_runs(i).ss_motor_current = ss_data(:, 3);
    PE_runs(i).ss_battery_voltage = ss_data(:, 4);
    PE_runs(i).ss_motor_voltage = ss_data(:, 5);

    PE_runs(i).num_samples = size(ss_data, 1);

    PE_runs(i).ss_battery_power = PE_runs(i).ss_battery_voltage .* PE_runs(i).ss_battery_current;  % W
    PE_runs(i).ss_motor_power = PE_runs(i).ss_motor_voltage .* PE_runs(i).ss_motor_current;        % W
    
    % Calculate averages for steady state
    PE_runs(i).avg_battery_current = mean(ss_data(:, 2));
    PE_runs(i).avg_motor_current = mean(ss_data(:, 3));
    PE_runs(i).avg_battery_voltage = mean(ss_data(:, 4));
    PE_runs(i).avg_motor_voltage = mean(ss_data(:, 5));
    
    fprintf('  Averages: Motor V=%.3f, Motor I=%.3f, Battery V=%.3f, Battery I=%.3f\n', ...
        PE_runs(i).avg_motor_voltage, PE_runs(i).avg_motor_current, ...
        PE_runs(i).avg_battery_voltage, PE_runs(i).avg_battery_current);
end

fprintf('\n✓ Processed %d runs successfully!\n\n', length(run_num_arr));

% %% Plot 1, voltage, current vs time
% Prompt user for run number
run_to_plot = input(sprintf('Enter run number to plot (1 to %d): ', length(run_num_arr)));

% Validate input
if run_to_plot < 1 || run_to_plot > length(run_num_arr)
    error('Invalid run number! Must be between 1 and %d', length(run_num_arr));
end

% Get data for selected run
selected_run = PE_runs(run_to_plot);

% Convert time to seconds starting from 0
time_ms = selected_run.ss_millis;
time_s = (time_ms - time_ms(1)) / 1000;  % Start from 0 and convert to seconds

% Create figure with two y-axes
figure('Name', sprintf('Run %d - Steady State Data', run_to_plot));

% Plot current on left y-axis
yyaxis left
plot(time_s, selected_run.ss_motor_current, '-b', 'LineWidth', 1.5, 'DisplayName', 'Motor Current');
hold on;
plot(time_s, selected_run.ss_battery_current, '--b', 'LineWidth', 1.5, 'DisplayName', 'Battery Current');
ylabel('Current (A)');
ylim_left = ylim;  % Store left axis limits
ax = gca;
ax.YColor = 'b';  % Set left y-axis color to blue

% Plot voltage on right y-axis
yyaxis right
plot(time_s, selected_run.ss_motor_voltage, '-r', 'LineWidth', 1.5, 'DisplayName', 'Motor Voltage');
hold on;
plot(time_s, selected_run.ss_battery_voltage, '--r', 'LineWidth', 1.5, 'DisplayName', 'Battery Voltage');
ylabel('Voltage (V)');
ax.YColor = 'r';  % Set right y-axis color to red

% Labels and formatting
xlabel('Time (s)');
title(sprintf('Run %d: Steady State (Throttle = %d%%)', run_to_plot, selected_run.throttle));
legend('Location', 'best');
ylim([0 30]);
grid on;

% Display averages on plot
dim = [0.15 0.7 0.3 0.2];  % Position of annotation box
str = sprintf(['Averages:\n' ...
               'Motor Current: %.3f A\n' ...
               'Battery Current: %.3f A\n' ...
               'Motor Voltage: %.3f V\n' ...
               'Battery Voltage: %.3f V'], ...
               selected_run.avg_motor_current, ...
               selected_run.avg_battery_current, ...
               selected_run.avg_motor_voltage, ...
               selected_run.avg_battery_voltage);
annotation('textbox', dim, 'String', str, 'FitBoxToText', 'on', ...
           'BackgroundColor', 'white', 'EdgeColor', 'black');

fprintf('\nPlotted Run %d (Throttle = %d%%)\n', run_to_plot, selected_run.throttle);

%% Second Plot - Power vs Time
% Prompt user for run number
run_to_plot2 = input(sprintf('Enter run number to plot power data (1 to %d): ', length(run_num_arr)));

% Validate input
if run_to_plot2 < 1 || run_to_plot2 > length(run_num_arr)
    error('Invalid run number! Must be between 1 and %d', length(run_num_arr));
end

% Get data for selected run
selected_run2 = PE_runs(run_to_plot2);

% Convert time to seconds starting from 0
time_ms2 = selected_run2.ss_millis;
time_s2 = (time_ms2 - time_ms2(1)) / 1000;  % Start from 0 and convert to seconds

% Calculate mechanical power from Excel data
torque_run = torque_arr(run_to_plot2);  % Nm
speed_rpm = speed_arr(run_to_plot2);     % RPM
speed_rad_s = speed_rpm * (2*pi/60);     % Convert to rad/s
mechanical_power = torque_run * speed_rad_s;  % W

% Create figure for power
figure('Name', sprintf('Run %d - Power Data', run_to_plot2));

plot(time_s2, selected_run2.ss_battery_power, '-b', 'LineWidth', 1.5, 'DisplayName', 'Battery Power');
hold on;
plot(time_s2, selected_run2.ss_motor_power, '-r', 'LineWidth', 1.5, 'DisplayName', 'Motor Power');
plot(time_s2, mechanical_power * ones(size(time_s2)), '-g', 'LineWidth', 1.5, 'DisplayName', 'Mechanical Power');
ylabel('Power (W)');
xlabel('Time (s)');
title(sprintf('Run %d: Steady State Power (Throttle = %d%%)', run_to_plot2, selected_run2.throttle));
legend('Location', 'best');
grid on;

% Calculate average powers and efficiencies
avg_battery_power = mean(selected_run2.ss_battery_power);
avg_motor_power = mean(selected_run2.ss_motor_power);
PE_efficiency = (avg_motor_power / avg_battery_power) * 100;  % %
mechanical_efficiency = (mechanical_power / avg_motor_power) * 100;  % %
overall_efficiency = (PE_efficiency / 100) * (mechanical_efficiency / 100) * 100;  % %

dim2 = [0.15 0.7 0.3 0.2];
str2 = sprintf(['Average Powers:\n' ...
                'Battery Power: %.3f W\n' ...
                'Motor Power: %.3f W\n' ...
                'Mechanical Power: %.3f W\n' ...
                '\\eta_{PE}: %.2f%%\n' ...
                '\\eta_{mech}: %.2f%%\n' ...
                '\\eta_{overall}: %.2f%%'], ...
                avg_battery_power, ...
                avg_motor_power, ...
                mechanical_power, ...
                PE_efficiency, ...
                mechanical_efficiency, ...
                overall_efficiency);
annotation('textbox', dim2, 'String', str2, 'FitBoxToText', 'on', ...
           'BackgroundColor', 'white', 'EdgeColor', 'black', 'Interpreter', 'tex');

fprintf('\nPlotted Power for Run %d (Throttle = %d%%)\n', run_to_plot2, selected_run2.throttle);
fprintf('PE Efficiency: %.2f%%, Mechanical Efficiency: %.2f%%, Overall Efficiency: %.2f%%\n', ...
    PE_efficiency, mechanical_efficiency, overall_efficiency);
%% Third Plot - 3D Efficiency Map
% Calculate overall efficiency for all runs
overall_eff_array = zeros(length(run_num_arr), 1);
wheel_diameter = 0.508;  % m
wheel_circumference = pi * wheel_diameter;  % m

% Convert speed to road speed (km/h)
road_speed_kmh = (speed_arr * wheel_circumference * 60) / 1000;  % km/h

for i = 1:length(run_num_arr)
    % Calculate average powers
    avg_batt_pwr = mean(PE_runs(i).ss_battery_power);
    avg_mot_pwr = mean(PE_runs(i).ss_motor_power);

    % Calculate mechanical power
    torque_i = torque_arr(i);
    speed_rpm_i = speed_arr(i);
    speed_rad_s_i = speed_rpm_i * (2*pi/60);
    mech_pwr_i = torque_i * speed_rad_s_i;

    % Calculate efficiencies
    PE_eff_i = (avg_mot_pwr / avg_batt_pwr) * 100;
    mech_eff_i = (mech_pwr_i / avg_mot_pwr) * 100;
    overall_eff_array(i) = (PE_eff_i / 100) * (mech_eff_i / 100) * 100;
end

% Create grid for surface plot
[speed_grid, torque_grid] = meshgrid(...
    linspace(min(road_speed_kmh), max(road_speed_kmh), 50), ...
    linspace(min(torque_arr), max(torque_arr), 50));

% Interpolate efficiency values onto grid
eff_grid = griddata(road_speed_kmh, torque_arr, overall_eff_array, ...
                    speed_grid, torque_grid, 'cubic');

% Create 3D surface plot
figure('Name', '3D Efficiency Map');
surf(speed_grid, torque_grid, eff_grid, 'EdgeColor', 'none', 'FaceAlpha', 0.8);
hold on;
% Overlay scatter points
scatter3(road_speed_kmh, torque_arr, overall_eff_array, 30, 'k', 'filled');
hold off;

xlabel('Road Speed (km/h)');
ylabel('Torque (Nm)');
zlabel('\eta_{overall} (%)');
title('Overall Efficiency vs Road Speed and Torque');
colorbar;
colormap('jet');
grid on;
view(45, 30);  % Set viewing angle
shading interp;  % Smooth color transitions

fprintf('\n3D Efficiency surface plot created with %d runs\n', length(run_num_arr));
fprintf('Road speed range: %.2f to %.2f km/h\n', min(road_speed_kmh), max(road_speed_kmh));
%% Fourth Plot - 3D Efficiency Map (Voltage vs Current)
% Extract motor voltage and current for all runs
motor_voltage_array = zeros(length(run_num_arr), 1);
motor_current_array = zeros(length(run_num_arr), 1);

for i = 1:length(run_num_arr)
    motor_voltage_array(i) = PE_runs(i).avg_motor_voltage;
    motor_current_array(i) = PE_runs(i).avg_motor_current;
end

% Create grid for surface plot
[voltage_grid, current_grid] = meshgrid(...
    linspace(min(motor_voltage_array), max(motor_voltage_array), 50), ...
    linspace(min(motor_current_array), max(motor_current_array), 50));

% Interpolate efficiency values onto grid
eff_grid_vc = griddata(motor_voltage_array, motor_current_array, overall_eff_array, ...
                       voltage_grid, current_grid, 'cubic');

% Create 3D surface plot
figure('Name', '3D Efficiency Map - Voltage vs Current');
surf(voltage_grid, current_grid, eff_grid_vc, 'EdgeColor', 'none', 'FaceAlpha', 0.8);
hold on;

% Add slice at 10A
slice_current = 18;  % A
voltage_slice = linspace(min(motor_voltage_array), max(motor_voltage_array), 100);
current_slice = slice_current * ones(size(voltage_slice));
eff_slice = griddata(motor_voltage_array, motor_current_array, overall_eff_array, ...
                     voltage_slice, current_slice, 'cubic');
plot3(voltage_slice, current_slice, eff_slice, 'b-', 'LineWidth', 3);

% Overlay scatter points
scatter3(motor_voltage_array, motor_current_array, overall_eff_array, 30, 'k', 'filled');
hold off;

xlabel('Motor Voltage (V)');
ylabel('Motor Current (A)');
zlabel('\eta_{overall} (%)');
title('Overall Efficiency vs Motor Voltage and Current');
colorbar;
colormap('jet');
grid on;
view(45, 30);
shading interp;

% Add legend for slice
legend('Efficiency Surface', 'Slice at 18A', 'Data Points', 'Location', 'best');

fprintf('\n3D Efficiency plot (Voltage vs Current) created with slice at %.1f A\n', slice_current);