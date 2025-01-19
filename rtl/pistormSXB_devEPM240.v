/*
 * 19 JAN 2025
 *
 * Tested with 373 & 374 latches
 * Tested Pi4 with 1500/500 & 1800/500 clocks
 *
 * *****
 *
 * Blitter is working
 * FDD is working
 * ACSI bus is working - see's all partitions
 *
 * *****
 *
 * The following results are from Pi4 @ 1800/500
 *
 * tested configs
 * test000.cfg, test020.cfg, games.cfg, atari4.cfg 
 *
 * 68020 + FPU
 * loopcycles 300
 * MEMSPEED READ 3.7 MB/s / WRITE 3.7 MB/s
 * COREMARK 53 / 70
 * FRNTBNCH 3135
 * 
 * 20 years runs
 * Dread runs
 *
 * *****
 *
 * 68000
 * loopcycles 300
 * MEMSPEED READ 3.7 MB/s / WRITE 3.7 MB/s
 * COREMARK 2.700912
 * FRNTBNCH 1169
 * 
 * Age of Empires runs
 * Bad APPLE runs from IDE only
 * 20 years runs birthday screen then stops
 * Onyx runs smoothly
 * Fastbobs runs
 * Dread runs
 * 
 */

 
module pistormsxb_devEPM240(
    output reg     	PI_TXN_IN_PROGRESS, 	// GPIO0
	output reg     	PI_IPL1,        		// GPIO1
    input   [1:0]   PI_A,       			// GPIO[3..2]
	output reg      PI_IPL2,     			// GPIO4
    output      	PI_BERR,   				// GPIO5
    input           PI_RD,      			// GPIO6
    input           PI_WR,      			// GPIO7
    inout   [15:0]  PI_D,       			// GPIO[23..8]
 
    output reg      LTCH_A_0,
    output reg      LTCH_A_8,
    output reg    	LTCH_A_16,
    output reg     	LTCH_A_24,
    output          LTCH_A_OE_n,
    output          LTCH_D_RD_U,
    output          LTCH_D_RD_L,
    output      	LTCH_D_RD_OE_n,
    output reg     	LTCH_D_WR_U,
    output reg     	LTCH_D_WR_L,
    output          LTCH_D_WR_OE_n,

    input           M68K_CLK,
    output   [2:0]  M68K_FC,

    output       	M68K_AS_n,
    output       	M68K_UDS_n,
    output       	M68K_LDS_n,
    output       	M68K_RW,

    inout           M68K_DTACK_n,
    inout           M68K_BERR_n,

    input           M68K_VPA_n,
    output          M68K_E,
    output       	M68K_VMA_n,

    input   [2:0]   M68K_IPL_n,

    inout           M68K_RESET_n,
    inout           M68K_HALT_n,

    input           M68K_BR_n,
    output reg     	M68K_BG_n,
    input           M68K_BGACK_n
  );

	localparam REG_DATA 					= 2'd0;
	localparam REG_ADDR_LO 					= 2'd1;
	localparam REG_ADDR_HI 					= 2'd2;
	localparam REG_STATUS 					= 2'd3;
	
	localparam LO							= 1'b0;
	localparam HI							= 1'b1;
	
	localparam S0							= 3'd0;
	localparam S1							= 3'd1;
	localparam S2							= 3'd2;
	localparam S3							= 3'd3;
	localparam S4							= 3'd4;
	localparam S5							= 3'd5;
	localparam S6							= 3'd6;
	localparam S7							= 3'd7;
	
	localparam E0							= 4'd0;
	localparam E2							= 4'd2;
	localparam E5							= 4'd5;
	localparam E6							= 4'd6;
	localparam E8							= 4'd8;
	localparam E9							= 4'd9;
	localparam E10							= 4'd10;

	
	/* ******************************************** */

	reg [15:0] data_out;
	
	/*
	 * cryptodad Oct'24
	 *
	 * 11 bit register holding CPLD firmware revision
	 * should be set via .qsf file -> update_version.tcl
	 */
	//reg [10:0] fwrev = 16'h1;
	`include "version_reg.v"

	wire trigger;
	assign trigger = (PI_A == REG_STATUS && CMDRD);
		
	assign PI_D = trigger ? {ipl, fwrev, ~M68K_RESET_n, 1'b0} : 16'bz;		
	
	reg [15:0] status;
	wire reset_out 							= status[1]; /* ps_protocol.c -> ps_write_status_reg (STATUS_BIT_INIT) */
	wire reset_sm							= LO;//status[0] || (!M68K_RESET_n && !M68K_HALT_n);
	wire halt								= status[0];
	
	assign M68K_RESET_n 					= (reset_out) ? LO : 1'bz;
	assign M68K_HALT_n 						= (halt) ? LO : 1'bz;
	
	reg op_rw 								= HI;
	reg op_uds_n 							= HI;
	reg op_lds_n 							= HI;

	wire [2:0] FC_INT;
	wire AS_INT;
	wire UDS_INT;
	wire LDS_INT;
	wire RW_INT;
	wire VMA_INT;

	reg M68K_VMA_nr 						= 1'd1;
	
	reg s0									= 1'd1; //M68K bus states
	reg s1									= 1'd0;
	reg s2									= 1'd0;
	reg s3									= 1'd0;
	reg s4									= 1'd0;
	reg s5									= 1'd0;
	reg s6									= 1'd0;
	reg s7									= 1'd0;
	
	reg CMDRD;
	reg PIRD;
	always @( PI_RD ) begin
	
		PIRD 								<= PI_RD;
		
		if ( PIRD )
			CMDRD 							<= HI;
			
		else begin
		
			if ( CMDRD && !PIRD )
				CMDRD 						<= LO;
		end
	end
	
	
	reg a0;
	
	reg [2:0] op_fc 						= SVR_PGM;//CPU_SPACE;
	
	reg [2:0] ipl;
	reg [2:0] ipl_1;
	reg [2:0] ipl_2;
	reg [2:0] reset_d 						= 3'b000;
	
	
	/* INTERRUPT CONTROL */
	
	/* 
	 * check current IPL with previous IPL
	 * only report interrupt if current IP{L > previous IPL 
	 */
	
	/* IPL lines should be serviced on negative edge */
	always @( posedge c8m ) begin
	
		if ( c8m_rising ) begin
		/* only interested in interrupts when a RW has finished */
		//if ( !PI_TXN_IN_PROGRESS ) begin
		
			ipl 							<= ~M68K_IPL_n;
			PI_IPL1							<= ~M68K_IPL_n[1];
			PI_IPL2							<= ~M68K_IPL_n[2];
		end
			
		reset_d[2:1] 						<= { reset_d[1:0], M68K_RESET_n };
	end	
	
	
	/* RESET */
	reg [1:0] resetfilter					= 2'b11;
	wire oor 								= (resetfilter == 2'b01) || reset_sm; //pulse when out of reset. delay by one clock pulse is required to prevent lock after reset
	always @(negedge c8m) begin
	
		resetfilter 						<= {resetfilter[0], M68K_RESET_n};
	end
	
  
	/* E CLOCK */
	// A single period of clock E consists of 10 MC68000 clock periods (six clocks low, four clocks high)
	reg [3:0] e_counter = 4'd0;
	
	always @( posedge c8m ) begin
	
		if (e_counter == E9)
			e_counter 						<= E0;
			
		else
			e_counter 						<= e_counter + 4'd1;
	end
	
	assign M68K_E 							= (e_counter > E5) ? HI : LO; //six clocks low (0-5), four clocks high (6-9)
	

	/* Bus Arbitration */
	reg [3:0] BG_DELAY 						= 4'b1111;
	reg [5:0] BR_DELAY 						= 6'b111111;
	reg [5:0] BGACK_DELAY					= 6'b000000;

	
	assign c8m = aCLK[0];
	always @(M68K_CLK) begin
	
		aCLK 								<= { aCLK[1:0], M68K_CLK };
		
		case (aCLK)
		
			2'b01: begin
			
				c8m_rising 					<= HI;
				c8m_falling 				<= LO;
			end
			
			2'b10: begin
			
				c8m_rising 					<= LO;
				c8m_falling 				<= HI;
			end
		endcase
	end
	
	
	// Define FC bits
	parameter USER_DATA						= 3'b001;
	parameter USER_PGM						= 3'b010;
	parameter SVR_DATA						= 3'b101;
	parameter SVR_PGM						= 3'b110;
	parameter CPU_SPACE						= 3'b111;
	
	reg c8m_falling 						= HI;
	reg c8m_rising 							= LO;
	reg [2:0] aCLK;
	
	reg BGACKi;
	reg BRi;
	reg BGi									= HI;
	
	always @( c8m ) begin

		BGACK_DELAY							<= {BGACK_DELAY[4:0], M68K_BGACK_n};
		BGACKi								<= M68K_BGACK_n;
		BRi									<= M68K_BR_n;
		M68K_BG_n							<= BGi;		
	end

	
	/*
	   Bus Arbitration
	*/		
	
	/* bus grant should be asserted ASAP */

	reg gotBR = LO;
	reg ASSERT = LO;
	reg DEASSERT = HI;
	
	
	always @(negedge c8m) begin
	
		// Blitter requires address and data busses to be established 
		if ( BGi && gotBR && !AS_INT ) begin
		
			BGi 							<= LO;
		end
		
		else if ( (!BGi && !gotBR) || (s0 && AS_INT) ) begin
		
			BGi 							<= HI;
		end
		
		
	end
	
	
	always @(negedge c8m) begin
	
		BR_DELAY[5:0] <= {BR_DELAY[4:0], BRi};
		
		case (BR_DELAY[1:0])
		
			2'b10: begin
				gotBR 						<= HI;
				
			end
			
			2'b01: begin
											gotBR <= LO;
			end
		endcase
	end
	
	
	// posedge addrhi_done - upper address word is latched
	// then need to put 24 bit address on address bus
	// then can assert AS
	always @(addrhi_done, reset_sm, s6) begin
		
		if ( addrhi_done && !PI_TXN_IN_PROGRESS ) begin
		
			PI_TXN_IN_PROGRESS 				<= HI;
		end
		
		if ( s6 || reset_sm ) begin
		
			PI_TXN_IN_PROGRESS 				<= LO;
		end
	end
	
	
	/* WRITE commands */

	
	/*
	 * ideally make PI_WR autonomous
	 * posedge PI_WR = write command sent
	 */
	
	assign CMDWR = PI_WR;
	
	reg addrlo_done 						= LO;
	reg addrhi_done 						= LO;
	reg data_done 							= LO;
	
	always @(CMDWR) begin
	
		if ( CMDWR ) begin
		
			case (PI_A)
				
				REG_DATA: begin
				
					LTCH_D_WR_U 			<= HI;
					LTCH_D_WR_L 			<= HI;
					data_done				<= HI;
				end
				
				REG_ADDR_LO: begin
				
					a0 						<= PI_D[0];
					LTCH_A_0				<= HI;
					LTCH_A_8				<= HI;
					addrlo_done				<= HI;
				end
				
				// rising edge on latches/flip-flops, latches upper address 16 bit (actually 8bits)
				REG_ADDR_HI: begin
				
					op_rw 					<= PI_D[9];
					op_uds_n 				<= PI_D[8] ? a0 : LO;
					op_lds_n 				<= PI_D[8] ? !a0 : LO;
					op_fc 					<= PI_D[15:13];
					LTCH_A_16				<= HI;
					LTCH_A_24				<= HI;
					addrhi_done				<= HI;
				end
				
				REG_STATUS: begin
					status 					<= PI_D;
					
					/* if needing to use ps_fw_wr () then uncomment this,
					   but if left in, for some reason ps_fw_rd () returns
					   bad data, consistent, but bad */
					//if ( PI_D[15] == HI ) begin
					
					//	fwrev				<= PI_D[10:0];
					//end
				end
			endcase
		end

		else if ( !CMDWR ) begin
		
			if ( data_done ) begin
			
				LTCH_D_WR_U 				<= LO;
				LTCH_D_WR_L 				<= LO;
				data_done					<= LO;
			end
			
			if ( addrlo_done ) begin
			
				LTCH_A_0					<= LO;
				LTCH_A_8					<= LO;
				addrlo_done					<= LO;
			end
			
			if ( addrhi_done ) begin
			
				LTCH_A_16					<= LO;
				LTCH_A_24					<= LO;
				addrhi_done					<= LO;
			end
				
		end
	end 
	
	
	// Sync with 68K bus operations
	
	
	
	/* BUS TRANSFER STATE MACHINE */
	wire s0rst = s1;
	wire s1rst = s2 | oor;
	wire s2rst = s3 | oor;
	wire s3rst = s4 | oor;
	wire s4rst = s5 | oor;
	wire s5rst = s6 | oor;
	wire s6rst = s7 | oor;
	wire s7rst = s0 | oor;
	
	
	/*
	 * 680xx Bus-Cycle 
	 * Atari starts when BGK is high
	 */
	 always @(posedge c8m, posedge s0rst) begin
		if(s0rst)
		  s0<=1'd0;
		//else if(s7 | oor) begin
		//else if( (s7 | oor) && M68K_BERR_n && BGACKi ) begin
		else if( (s7 | oor) && BGACKi ) begin

		  s0 <= 1'b1;
		end
	end
	
	/* need to check BGACK here */
	always @(negedge c8m, posedge s1rst) begin
		if(s1rst)
		  s1<=1'd0;
		//else if(s0) begin
		//else if(s0 & PI_TXN_IN_PROGRESS & BGACKi)
		else if(s0 && BGACKi) begin // BGACKi here works for bad apple
		  s1<=1'd1;
		end
	end
	
	// checking PI_TXN_IN_PROGRESS here results in R/W of 3.4/3.2
	always @(posedge c8m, posedge s2rst) begin
		if(s2rst)
		  s2<=1'd0;
		else if(s1) begin
		//else if(s1 && PI_TXN_IN_PROGRESS && BGACKi) begin
		  s2<=1'd1;
		end
	end
	
	// checking PI_TXN_IN_PROGRESS here results in expected performance of 3.7/3.7
	always @(negedge c8m, posedge s3rst) begin
		if (s3rst)
		  s3<=1'd0;
		//else if(s2) begin
		//else if (s2 && PI_TXN_IN_PROGRESS && BGACKi) begin
		//else if (s2 && PI_TXN_IN_PROGRESS) begin
		else if (s2 && BGACKi) begin
		
		  s3 <= 1'd1;
		end
	end
	
	always @(posedge c8m, posedge s4rst) begin
		if(s4rst)
		  s4<=1'd0;
		else if(s3) begin
		  s4<=1'd1;
		end
	end
	
	always @(negedge c8m, posedge s5rst) begin
		if(s5rst)
		  s5<=1'd0;
		else if(s4 && (!M68K_DTACK_n || !M68K_BERR_n || (!M68K_VMA_nr && e_counter == E8))) begin
		  s5<=1'd1;
		end
	end
	
	always @(posedge c8m, posedge s6rst) begin
		if(s6rst)
		  s6<=1'd0;
		else if(s5) begin
		  s6<=1'd1;
		end
	end
	
	always @(negedge c8m, posedge s7rst) begin
		if(s7rst)
		  s7<=1'd0;
		else if(s6) begin
		  s7<=1'd1;
		end
	end
	
  
	// Entering S1, the processor drives a valid address on the address bus.
	// As the clock rises at the end of S7, the processor places the address and data buses in the high-impedance state
  
	assign FC_INT = ( PI_TXN_IN_PROGRESS && (s0|s1|s2|s3|s4|s5) ) || (s6|s7) ? op_fc : CPU_SPACE;
	
	assign PI_BERR = M68K_BERR_n;
	
	
	/* Data Bus Hi-Z on trailing edge of S7 */
	assign LTCH_D_WR_OE_n = ( PI_TXN_IN_PROGRESS && (s3|s4|s5|s6|s7) ) && M68K_BERR_n ? op_rw : HI;
	
	/* Address Bus Hi-Z on trailing edge of S7 */
	assign LTCH_A_OE_n = ( PI_TXN_IN_PROGRESS && (s1|s2|s3|s4|s5|s6|s7) ) && M68K_BERR_n ? LO : HI;
	
	assign LTCH_D_RD_OE_n = !(PI_A == REG_DATA && CMDRD);
	
	/* Data is driven on to the Bus in S6 */
	/* 374 edge d-type flip/flop - on rising edge, data is latched */
	assign LTCH_D_RD_U = ( PI_TXN_IN_PROGRESS && s5 ) ? HI : LO;
	assign LTCH_D_RD_L = ( PI_TXN_IN_PROGRESS && s5 ) ? HI : LO;
	

// On the rising edge of S2, the processor asserts AS and drives R/W low.
// On the falling edge of the clock entering S7, the processor negates AS, UDS, or LDS
	assign AS_INT = ( PI_TXN_IN_PROGRESS && (s2|s3|s4|s5) ) || (s6) ? LO : HI;
	

// READ : On the rising edge of state 2 (S2), the processor asserts AS and UDS, LDS, or DS
// WRITE : At the rising edge of S4, the processor asserts UDS, or LDS // wrong: DS should be set in s3
// On the falling edge of the clock entering S7, the processor negates AS, UDS, or LDS
	/* 
	 * read (op_rw == 1)
	 * write (op_rw == 0)
	 */
	assign UDS_INT = ( PI_TXN_IN_PROGRESS && (~op_rw & (s4|s5)) ) || (s6) ? op_uds_n : ( PI_TXN_IN_PROGRESS && (op_rw & (s2|s3|s4|s5)) ) || (s6) ? op_uds_n : HI;
	assign LDS_INT = ( PI_TXN_IN_PROGRESS && (~op_rw & (s4|s5)) ) || (s6) ? op_lds_n : ( PI_TXN_IN_PROGRESS && (op_rw & (s2|s3|s4|s5)) ) || (s6) ? op_lds_n : HI;
	
// On the rising edge of S2, the processor asserts AS and drives R/W low.
// As the clock rises at the end of S7, the processor drives R/W high
	assign RW_INT = ( PI_TXN_IN_PROGRESS && (s2|s3|s4|s5) ) || (s6|s7) ? op_rw : HI;
	
	
//	output reg      M68K_VMA_n,
	wire vmarst = (s7 | oor);
	always @( posedge c8m, posedge vmarst ) begin
	
		if ( vmarst )
			M68K_VMA_nr <= HI;
			
		else if ( s4 && !M68K_VPA_n && e_counter == E2 )
			M68K_VMA_nr <= LO;
	end
	
	assign VM_INT = M68K_VMA_nr ? HI : LO;
  

	assign M68K_FC 							= (M68K_BGACK_n & ~reset_out) ? FC_INT	: 3'bzzz;
	assign M68K_AS_n 						= (M68K_BGACK_n & ~reset_out) ? AS_INT 	: 1'bz;
	assign M68K_UDS_n 						= (M68K_BGACK_n & ~reset_out) ? UDS_INT : 1'bz;
	assign M68K_LDS_n 						= (M68K_BGACK_n & ~reset_out) ? LDS_INT : 1'bz;
	assign M68K_RW 							= (M68K_BGACK_n & ~reset_out) ? RW_INT 	: 1'bz;
	assign M68K_VMA_n						= (M68K_BGACK_n & ~reset_out) ? VMA_INT : 1'bz;
/*
	assign M68K_FC 							= BGACKi ? FC_INT	: 3'bzzz;
	assign M68K_AS_n 						= BGACKi ? AS_INT 	: 1'bz;
	assign M68K_UDS_n 						= BGACKi ? UDS_INT 	: 1'bz;
	assign M68K_LDS_n 						= BGACKi ? LDS_INT 	: 1'bz;
	assign M68K_RW 							= BGACKi ? RW_INT	: 1'bz;
	assign M68K_VMA_n						= BGACKi ? VMA_INT 	: 1'bz;
*/
endmodule