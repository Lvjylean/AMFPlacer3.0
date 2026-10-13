// Compile-only DFX fixture: JTAG supplies the clock and observes the result.
// There are no board I/O assignments and no DRC severity overrides.
module static_top;
  wire tck, tdi, shift, reset, sel;
  wire clk;
  reg [7:0] source = 8'h01;
  reg [7:0] sink = 0;
  wire [7:0] result;
  BSCANE2 #(.JTAG_CHAIN(1)) jtag (
    .TCK(tck), .TDI(tdi), .SHIFT(shift), .RESET(reset), .SEL(sel),
    .TDO(^sink), .CAPTURE(), .DRCK(), .RUNTEST(), .TMS(), .UPDATE());
  BUFG clock_buffer (.I(tck), .O(clk));
  always @(posedge clk) begin
    source <= {source[6:0], source[7] ^ source[5] ^ tdi};
    sink <= result;
  end
  (* bblock = "true", keep_hierarchy = "yes" *) rm rm_inst
    (.clk(clk), .data_in(source), .data_out(result));
endmodule
