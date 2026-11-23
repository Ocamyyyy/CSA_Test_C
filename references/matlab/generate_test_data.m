function report = generate_test_data(output_root)
%GENERATE_TEST_DATA Generate 2048-point complex FFT DMA test vectors.

arguments
    output_root (1,1) string = string(fileparts(mfilename('fullpath')))
end

N = 2048;
config.fft_length = N;
config.mode_sel_binary = '01';
config.config_tdata_hex = '0x0D550B';
config.direction = 'forward FFT';
config.scale_schedule_hex = '0x6AA';
config.total_right_shift_bits = 11;
config.output_scale = 1/N;
config.output_order = 'natural';
config.rounding = 'convergent rounding inside XFFT';
config.input_component_bits = 16;
config.output_component_bits = 16;
config.word_bits = 32;
config.word_layout = '[31:16]=signed int16 Q, [15:0]=signed int16 I';
config.file_byte_order = 'little-endian uint32';
config.input_bytes = N*4;
config.output_bytes = N*4;

cases = struct([]);

input_i = zeros(N,1,'int16');
input_q = zeros(N,1,'int16');
input_i(1) = int16(16384);
cases(1).name = 'impulse';
cases(1).input_i = input_i;
cases(1).input_q = input_q;
cases(1).description = 'x[0] = 16384 + j0; all remaining samples are zero';
cases(1).expected_peak_bin_zero_based = NaN;

n = (0:N-1).';
tone_bin = 37;
tone_amplitude = 8192;
input_i = int16(round(tone_amplitude*cos(2*pi*tone_bin*n/N)));
input_q = int16(round(tone_amplitude*sin(2*pi*tone_bin*n/N)));
cases(2).name = 'tone';
cases(2).input_i = input_i;
cases(2).input_q = input_q;
cases(2).description = 'positive complex tone, k=37, amplitude=8192';
cases(2).expected_peak_bin_zero_based = tone_bin;

random_seed = 20261111;
rng(random_seed,'twister');
random_components = randi([-8192,8191],N,2);
cases(3).name = 'random';
cases(3).input_i = int16(random_components(:,1));
cases(3).input_q = int16(random_components(:,2));
cases(3).description = sprintf( ...
    'uniform signed components in [-8192,8191], rng seed %d',random_seed);
cases(3).expected_peak_bin_zero_based = NaN;

fprintf('=== FFTTest 2048 DMA vector generation ===\n');
all_pass = true;
for index = 1:numel(cases)
    case_dir = fullfile(output_root,cases(index).name);
    if ~isfolder(case_dir), mkdir(case_dir); end

    input_i = cases(index).input_i;
    input_q = cases(index).input_q;
    input_complex = complex(double(input_i),double(input_q));
    ideal_fft_unscaled = fft(input_complex,N);
    ideal_fft_scaled = ideal_fft_unscaled/N;

    case_metadata.name = cases(index).name;
    case_metadata.description = cases(index).description;
    case_metadata.expected_peak_bin_zero_based = ...
        cases(index).expected_peak_bin_zero_based;
    case_metadata.input_saturation_count = nnz( ...
        input_i==intmin('int16') | input_i==intmax('int16') ...
        | input_q==intmin('int16') | input_q==intmax('int16'));
    case_metadata.max_abs_i = max(abs(double(input_i)));
    case_metadata.max_abs_q = max(abs(double(input_q)));
    case_metadata.input_rms = sqrt(mean(abs(input_complex).^2));

    input_path = fullfile(case_dir,'fft_input.bin');
    write_complex_words(input_path,input_i,input_q);
    reference_path = fullfile(case_dir,'matlab_reference.mat');
    save(reference_path,'input_i','input_q','input_complex', ...
        'ideal_fft_unscaled','ideal_fft_scaled','config','case_metadata','-v7');

    input_info = dir(input_path);
    reference_info = dir(reference_path);
    sample_count = input_info.bytes/4;
    case_pass = input_info.bytes==config.input_bytes ...
        && sample_count==N && case_metadata.input_saturation_count==0;
    all_pass = all_pass && case_pass;

    report.cases(index).name = cases(index).name;
    report.cases(index).input_path = input_path;
    report.cases(index).input_bytes = input_info.bytes;
    report.cases(index).reference_path = reference_path;
    report.cases(index).reference_bytes = reference_info.bytes;
    report.cases(index).input_saturation_count = ...
        case_metadata.input_saturation_count;
    report.cases(index).pass = case_pass;
    fprintf('%-8s input=%d bytes samples=%d saturation=%d pass=%d\n', ...
        cases(index).name,input_info.bytes,sample_count, ...
        case_metadata.input_saturation_count,case_pass);
end

report.config = config;
report.pass = all_pass;
report.note = ['fft_expected.bin is intentionally not generated: the ' ...
    'double-precision scaled FFT does not model XFFT stage-by-stage ' ...
    'convergent rounding and is not bit-exact.'];
fprintf('FFTVALIDATION_DATA_GENERATION_PASS=%d\n',all_pass);
assert(all_pass,'FFTValidation:GenerationFailed','Generated data validation failed.');
end

function write_complex_words(path_text,input_i,input_q)
real_bits = uint16(mod(double(input_i),65536));
imag_bits = uint16(mod(double(input_q),65536));
packed = bitor(uint32(real_bits),bitshift(uint32(imag_bits),16));
fid = fopen(path_text,'w','ieee-le');
assert(fid>=0,'FFTValidation:OpenFailed','Cannot open %s',path_text);
cleanup = onCleanup(@() fclose(fid));
count = fwrite(fid,packed,'uint32');
assert(count==numel(packed),'FFTValidation:ShortWrite','Short write: %s',path_text);
end
