
`timescale 1ns / 1ps

module fft_config_ctrl (
    input  wire        aclk,
    input  wire        aresetn,

    // CPU control via AXI GPIO
    // mode_sel[1]: 0=2048, 1=4096
    // mode_sel[0]: 1=FFT,  0=IFFT
    input  wire [1:0]  mode_sel,

    // Rising-edge triggered configuration request
    input  wire        config_req,

    // AXI4-Stream configuration interface
    (* X_INTERFACE_IGNORE = "true" *)
    output wire [23:0] config_tdata,

    (* X_INTERFACE_IGNORE = "true" *)
    output wire        config_tvalid,

    (* X_INTERFACE_IGNORE = "true" *)
    input  wire        config_tready,

    // 1: configuration accepted by FFT IP
    output wire        config_done
);

    reg        req_d;
    reg        pending;
    reg        done;
    reg [1:0]  active_mode;

    wire req_rising;

    wire [4:0]  nfft;
    wire        fwd_inv;
    wire [11:0] scale_sch;

    // Detect CPU request rising edge
    assign req_rising = config_req & ~req_d;

    // Transform length: N = 2^NFFT
    assign nfft = active_mode[1] ? 5'd12 : 5'd11;

    // 1 = forward FFT, 0 = inverse FFT
    assign fwd_inv = active_mode[0];

    // Pipelined Streaming I/O scaling schedule
    // 2048: total right shift = 11 bits
    // 4096: total right shift = 12 bits
    assign scale_sch = active_mode[1]
                     ? 12'hAAA
                     : 12'h6AA;

    // FFT v9.1 configuration layout:
    // [23:21] padding
    // [20:9]  SCALE_SCH
    // [8]     FWD_INV
    // [7:5]   padding
    // [4:0]   NFFT
    assign config_tdata = {
        3'b000,
        scale_sch,
        fwd_inv,
        3'b000,
        nfft
    };

    assign config_tvalid = pending;
    assign config_done   = done;

    always @(posedge aclk) begin
        if (!aresetn) begin
            req_d       <= 1'b0;
            pending     <= 1'b0;
            done        <= 1'b0;
            active_mode <= 2'b00;
        end
        else begin
            req_d <= config_req;

            if (pending) begin
                // Configuration handshake completed
                if (config_tready) begin
                    pending <= 1'b0;
                    done    <= 1'b1;
                end
            end
            else if (req_rising) begin
                // Latch new configuration
                active_mode <= mode_sel;
                pending     <= 1'b1;
                done        <= 1'b0;
            end
        end
    end

endmodule
