(* bblock = "true" *)
module rm(input clk, input [7:0] data_in, output reg [7:0] data_out);
  always @(posedge clk) data_out <= {data_in[6:0], data_in[7]} ^ 8'h5a;
endmodule
