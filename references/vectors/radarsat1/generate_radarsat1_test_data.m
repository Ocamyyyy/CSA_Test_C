function report = generate_radarsat1_test_data(output_dir,repo_dir)
%GENERATE_RADARSAT1_TEST_DATA Extract one real 2048-point azimuth FFT frame.

arguments
    output_dir (1,1) string = string(fileparts(mfilename('fullpath')))
    repo_dir (1,1) string = ...
        "D:\Workspace\SAR-Synthetic-Aperture-Radar"
end

golden_dir = fullfile(repo_dir,'experiments','radarsat1_full_golden');
assert(isfolder(golden_dir),'Radarsat1Extraction:MissingGoldenModel', ...
    'Golden model directory not found: %s',golden_dir);
addpath(golden_dir);
if ~isfolder(output_dir), mkdir(output_dir); end

params = radarsat1_full_params();
[input_data,metadata] = load_radarsat1_full_data(params);
assert(isequal(size(input_data),[2048,3000]), ...
    'Radarsat1Extraction:BadInputSize','Expected input_data size 2048x3000.');
assert(isa(input_data,'double') && ~isreal(input_data) ...
    && all(isfinite(input_data(:))), ...
    'Radarsat1Extraction:BadInput','input_data must be finite complex double.');

search_columns = 1:params.source_dimensions(2);
column_energy = sum(abs(input_data(:,search_columns)).^2,1);
valid_columns = find(isfinite(column_energy) & column_energy>0);
assert(~isempty(valid_columns),'Radarsat1Extraction:NoValidColumn', ...
    'No nonzero finite real-data column found in columns 1:2048.');
[~,local_index] = max(column_energy(valid_columns));
selected_column = valid_columns(local_index);
x = input_data(:,selected_column);
assert(isequal(size(x),[2048,1]) && all(isfinite(x)) && nnz(x)>0);

nonzero_samples = nnz(x);
max_magnitude = max(abs(x));
rms_magnitude = sqrt(mean(abs(x).^2));
max_abs_real = max(abs(real(x)));
max_abs_imag = max(abs(imag(x)));
component_peak = max(max_abs_real,max_abs_imag);
target_component = 0.90*double(intmax('int16'));
scale_factor = target_component/component_peak;

real_unclipped = round(real(x)*scale_factor);
imag_unclipped = round(imag(x)*scale_factor);
real_saturation_count = nnz(real_unclipped>double(intmax('int16')) ...
    | real_unclipped<double(intmin('int16')));
imag_saturation_count = nnz(imag_unclipped>double(intmax('int16')) ...
    | imag_unclipped<double(intmin('int16')));
input_saturation_count = real_saturation_count+imag_saturation_count;
assert(input_saturation_count==0, ...
    'Radarsat1Extraction:Saturation','Unexpected int16 saturation.');

input_i = int16(real_unclipped);
input_q = int16(imag_unclipped);
x_quantized = complex(double(input_i),double(input_q));
input_complex = x_quantized; % Compatibility with compare_fpga_output.m
x_dequantized = x_quantized/scale_factor;
quantization_error = x_dequantized-x;
quantization_relative_l2 = norm(quantization_error)/norm(x);
quantization_max_abs_error = max(abs(quantization_error));
quantization_rmse = sqrt(mean(abs(quantization_error).^2));

ideal_fft_unscaled = fft(x_quantized,2048);
fft_reference = ideal_fft_unscaled/2048;
ideal_fft_scaled = fft_reference; % Compatibility with compare_fpga_output.m

config.fft_length = 2048;
config.mode_sel_binary = '01';
config.config_tdata_hex = '0x0D550B';
config.direction = 'forward FFT';
config.scale_schedule_hex = '0x6AA';
config.total_right_shift_bits = 11;
config.output_scale = 1/2048;
config.output_order = 'natural';
config.rounding = 'convergent rounding inside XFFT';
config.input_component_bits = 16;
config.output_component_bits = 16;
config.word_bits = 32;
config.word_layout = '[31:16]=signed int16 Q, [15:0]=signed int16 I';
config.file_byte_order = 'little-endian uint32';
config.input_bytes = 8192;
config.output_bytes = 8192;

case_metadata.name = 'radarsat1';
case_metadata.description = sprintf([ ...
    'RADARSAT-1 input_data(:,%d), maximum-energy nonzero column ' ...
    'selected from original-data columns 1:2048'],selected_column);
case_metadata.expected_peak_bin_zero_based = NaN;
case_metadata.input_saturation_count = input_saturation_count;
case_metadata.max_abs_i = max(abs(double(input_i)));
case_metadata.max_abs_q = max(abs(double(input_q)));
case_metadata.input_rms = sqrt(mean(abs(input_complex).^2));

data_source.repository = char(repo_dir);
data_source.parameter_function = 'radarsat1_full_params';
data_source.loader_function = 'load_radarsat1_full_data';
data_source.source_file = params.source_file;
data_source.source_variable = params.source_variable;
data_source.source_dimensions = metadata.source_dimensions;
data_source.processing_dimensions = metadata.processing_dimensions;
data_source.placement_rows = metadata.placement_rows;
data_source.placement_columns = metadata.placement_columns;

input_path = fullfile(output_dir,'fft_input.bin');
write_complex_words(input_path,input_i,input_q);
reference_path = fullfile(output_dir,'matlab_reference.mat');
save(reference_path,'x','selected_column','scale_factor','input_i','input_q', ...
    'x_quantized','input_complex','x_dequantized','ideal_fft_unscaled', ...
    'fft_reference','ideal_fft_scaled','config','case_metadata', ...
    'data_source','metadata','nonzero_samples','max_magnitude', ...
    'rms_magnitude','max_abs_real','max_abs_imag', ...
    'input_saturation_count','real_saturation_count', ...
    'imag_saturation_count','quantization_relative_l2', ...
    'quantization_max_abs_error','quantization_rmse','-v7');

[read_i,read_q,input_bytes] = read_complex_words(input_path);
binary_readback_exact = isequal(read_i,input_i) && isequal(read_q,input_q);
loaded_reference = load(reference_path,'x_quantized','fft_reference');
reference_recomputed = fft(loaded_reference.x_quantized,2048)/2048;
reference_max_abs_error = max(abs( ...
    reference_recomputed-loaded_reference.fft_reference));

pass = isequal(size(input_data),[2048,3000]) ...
    && selected_column>=1 && selected_column<=2048 ...
    && nonzero_samples>0 && numel(x)==2048 && input_bytes==8192 ...
    && binary_readback_exact && input_saturation_count==0 ...
    && reference_max_abs_error==0;

report.pass = pass;
report.selected_column = selected_column;
report.selection_rule = 'maximum energy among nonzero finite columns 1:2048';
report.column_energy = column_energy(selected_column);
report.nonzero_samples = nonzero_samples;
report.max_magnitude = max_magnitude;
report.rms_magnitude = rms_magnitude;
report.scale_factor = scale_factor;
report.input_bytes = input_bytes;
report.input_saturation_count = input_saturation_count;
report.quantization_relative_l2 = quantization_relative_l2;
report.quantization_max_abs_error = quantization_max_abs_error;
report.quantization_rmse = quantization_rmse;
report.binary_readback_exact = binary_readback_exact;
report.reference_max_abs_error = reference_max_abs_error;
report.processing_dimensions = size(input_data);
report.source_dimensions = metadata.source_dimensions;

report_path = fullfile(output_dir,'extraction_report.txt');
fid = fopen(report_path,'w','n','UTF-8');
assert(fid>=0,'Radarsat1Extraction:ReportOpen','Cannot open extraction report.');
cleanup_report = onCleanup(@() fclose(fid));
fprintf(fid,'RADARSAT-1 REAL DATA FFT EXTRACTION\n');
fprintf(fid,'source_repository=%s\n',repo_dir);
fprintf(fid,'loader=radarsat1_full_params + load_radarsat1_full_data\n');
fprintf(fid,'processing_dimensions=2048x3000\n');
fprintf(fid,'source_dimensions=%dx%d\n',metadata.source_dimensions);
fprintf(fid,'selection_rule=%s\n',report.selection_rule);
fprintf(fid,'selected_column=%d\n',selected_column);
fprintf(fid,'column_energy=%.17g\n',report.column_energy);
fprintf(fid,'nonzero_samples=%d\n',nonzero_samples);
fprintf(fid,'max_magnitude=%.17g\n',max_magnitude);
fprintf(fid,'rms_magnitude=%.17g\n',rms_magnitude);
fprintf(fid,'max_abs_real=%.17g\n',max_abs_real);
fprintf(fid,'max_abs_imag=%.17g\n',max_abs_imag);
fprintf(fid,'scale_factor=%.17g\n',scale_factor);
fprintf(fid,'quantization_relative_l2=%.17g\n',quantization_relative_l2);
fprintf(fid,'quantization_max_abs_error=%.17g\n',quantization_max_abs_error);
fprintf(fid,'quantization_rmse=%.17g\n',quantization_rmse);
fprintf(fid,'input_bytes=%d\n',input_bytes);
fprintf(fid,'binary_readback_exact=%d\n',binary_readback_exact);
fprintf(fid,'input_saturation_count=%d\n',input_saturation_count);
fprintf(fid,'reference_max_abs_error=%.17g\n',reference_max_abs_error);
fprintf(fid,'RADARSAT1_REALDATA_EXTRACTION_PASS=%d\n',pass);
clear cleanup_report;

fprintf('RADARSAT1_REALDATA_EXTRACTION_PASS=%d\n',pass);
fprintf('selected_column=%d\n',selected_column);
fprintf('nonzero_samples=%d\n',nonzero_samples);
fprintf('scale_factor=%.17g\n',scale_factor);
fprintf('input_bytes=%d\n',input_bytes);
fprintf('input_saturation_count=%d\n',input_saturation_count);
assert(pass,'Radarsat1Extraction:ValidationFailed', ...
    'RADARSAT-1 real-data extraction validation failed.');
end

function write_complex_words(path_text,input_i,input_q)
real_bits = uint16(mod(double(input_i),65536));
imag_bits = uint16(mod(double(input_q),65536));
packed = bitor(uint32(real_bits),bitshift(uint32(imag_bits),16));
fid = fopen(path_text,'w','ieee-le');
assert(fid>=0,'Radarsat1Extraction:OpenFailed','Cannot open %s',path_text);
cleanup = onCleanup(@() fclose(fid));
count = fwrite(fid,packed,'uint32');
assert(count==numel(packed),'Radarsat1Extraction:ShortWrite', ...
    'Short write: %s',path_text);
end

function [input_i,input_q,file_bytes] = read_complex_words(path_text)
info = dir(path_text);
file_bytes = info.bytes;
fid = fopen(path_text,'r','ieee-le');
assert(fid>=0,'Radarsat1Extraction:OpenFailed','Cannot open %s',path_text);
cleanup = onCleanup(@() fclose(fid));
packed = fread(fid,inf,'*uint32');
input_i = signed16(bitand(packed,uint32(65535)));
input_q = signed16(bitshift(packed,-16));
end

function values = signed16(unsigned_values)
values = double(unsigned_values);
negative = values>=32768;
values(negative) = values(negative)-65536;
values = int16(values);
end
