// Compile-only multi-SLR DFX fixture. Static bridge separates RP interfaces.
module static_top;
  wire tck, tdi, reset, clk;
  reg [7:0] source = 8'h01;
  (* keep = "true" *) reg [7:0] bridge = 0;
  reg [7:0] sink = 0;
  wire [7:0] a_out, b_out;
  BSCANE2 #(.JTAG_CHAIN(1)) jtag (.TCK(tck), .TDI(tdi), .RESET(reset),
    .TDO(^sink), .CAPTURE(), .DRCK(), .RUNTEST(), .SEL(), .SHIFT(), .TMS(), .UPDATE());
  BUFG clock_buffer (.I(tck), .O(clk));
  always @(posedge clk) begin
    source <= {source[6:0], source[7] ^ source[5] ^ tdi};
    bridge <= a_out;
    sink <= b_out;
  end
  (* bblock = "true", keep_hierarchy = "yes" *) rm rp_a
    (.clk(clk), .data_in(source), .data_out(a_out));
  (* bblock = "true", keep_hierarchy = "yes" *) rm rp_b
    (.clk(clk), .data_in(bridge), .data_out(b_out));
endmodule
