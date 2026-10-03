import sys
from pathlib import Path
import tempfile
import unittest
import collections
from types import SimpleNamespace

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts/diagnostics'))
from verify_minimap2_logic_rewrites import Design, verify, parameters, Pool


class LogicRewrites(unittest.TestCase):
    def run_case(self, change=''):
        base='''module minimap2_functional(c,d,y);
input c;
input d;
output y;
  (* KEEP = "true" *) wire q;
FDRE #(.INIT(1'b0)) ff (.C(c), .CE(1'b1), .D(d), .R(1'b0), .Q(q));
LUT1 #(.INIT(2'h2)) sink (.I0(q), .O(y));
endmodule
'''
        result=base.replace('wire q;', 'wire q;\n  wire copy;\n  wire buffered;').replace('.I0(q)', '.I0(buffered)').replace('endmodule',
            "FDRE #(.INIT(1'b0)) ff_replica (.C(c), .CE(1'b1), .D(d), .R(1'b0), .Q(copy));\n"
            'BUFGCE #(.CE_TYPE("ASYNC")) buf1 (.CE(1\'b1), .I(copy), .O(buffered));\nendmodule')
        if change=='input':result=result.replace('ff_replica (.C(c), .CE(1\'b1), .D(d)', 'ff_replica (.C(c), .CE(1\'b1), .D(c)')
        if change=='init':result=result.replace("FDRE #(.INIT(1'b0)) ff_replica", "FDRE #(.INIT(1'b1)) ff_replica")
        if change=='lut':result=result.replace(".INIT(2'h2)",".INIT(2'h1)")
        if change=='enable':result=result.replace("buf1 (.CE(1'b1)","buf1 (.CE(d)")
        with tempfile.TemporaryDirectory() as work:
            root=Path(work);(root/'a.v').write_text(base);(root/'b.v').write_text(result)
            return verify(Design(root/'a.v'),Design(root/'b.v'),root)

    def test_clone_and_constant_enable_buffer_pass(self):
        report=self.run_case()
        self.assertTrue(report['visible_structural_certificate_passed'])
        self.assertEqual(len(report['replica_rewrites']),1)
        self.assertFalse(report['full_design_formal_equivalence_proven'])

    def test_wrong_clone_input_is_rejected(self):
        self.assertFalse(self.run_case('input')['visible_structural_certificate_passed'])

    def test_wrong_clone_initial_state_is_rejected(self):
        self.assertFalse(self.run_case('init')['visible_structural_certificate_passed'])

    def test_changed_lut_function_is_rejected(self):
        self.assertFalse(self.run_case('lut')['visible_structural_certificate_passed'])

    def test_dynamic_buffer_enable_is_rejected(self):
        with self.assertRaises(ValueError):self.run_case('enable')

    def test_implicit_and_explicit_fdre_default_parameters(self):
        self.assertEqual(parameters('FDRE',{'INIT':"1'b0"}.items()),
                         parameters('FDRE',{'INIT':"1'b0",'IS_C_INVERTED':"1'b0",'IS_R_INVERTED':"1'b0",'IS_D_INVERTED':"1'b0"}.items()))

    def test_equal_lut_driver_substitution(self):
        text='''module minimap2_functional(a,b,y);
input a;
input b;
output y;
  wire x;
  wire z;
LUT2 #(.INIT(4'h8)) gate1 (.I0(a),.I1(b),.O(x));
LUT2 #(.INIT(4'h8)) gate2 (.I0(a),.I1(b),.O(z));
LUT1 #(.INIT(2'h2)) sink (.I0(x),.O(y));
endmodule
'''
        with tempfile.TemporaryDirectory() as work:
            r=Path(work);(r/'a.v').write_text(text);(r/'b.v').write_text(text.replace('sink (.I0(x)','sink (.I0(z)'))
            self.assertTrue(verify(Design(r/'a.v'),Design(r/'b.v'),r)['visible_structural_certificate_passed'])

    def test_async_reset_replicas_require_equal_controls_and_initial_state(self):
        for mismatch in ('none','PRE','INIT'):
            pool=Pool();clk=pool.add('c');d=pool.add('d');pre=pool.add('pre');q1=pool.add('q1');q2=pool.add('q2')
            init={'INIT':"1'b1",'IS_C_INVERTED':"1'b0",'IS_D_INVERTED':"1'b0",'IS_PRE_INVERTED':"1'b0"}
            pins={'C':(clk,),'CE':(1,),'D':(d,),'PRE':(pre,),'Q':(q1,)}
            alt=dict(pins,Q=(q2,));altinit=dict(init)
            if mismatch=='PRE':alt['PRE']=(d,)
            if mismatch=='INIT':altinit['INIT']="1'b0"
            design=SimpleNamespace(pool=pool,cells={'a':('FDPE',tuple(sorted(init.items())),pins),'b':('FDPE',tuple(sorted(altinit.items())),alt)})
            from verify_minimap2_logic_rewrites import duplicate_luts
            duplicate_luts(design)
            self.assertEqual(pool.root(q1)==pool.root(q2),mismatch=='none')

    def dsp_case(self, mutation=None):
        designs=[]
        for changed in (False,True):
            pool=Pool();clock=pool.add('clk');data=[pool.add('a'+str(i)) for i in range(17)];out=pool.add('p')
            cells={};a=data[-1:]*13+list(reversed(data))
            if changed:
                regs=[]
                for i,bit in enumerate(data):
                    q=pool.add('q'+str(i));regs.append(q)
                    pins={'C':(clock,),'D':(bit,),'CE':(1,),'R':(0,),'Q':(q,)}
                    pars={'INIT':"1'b0"}
                    if i==0:
                        if mutation=='D':pins['D']=(data[1],)
                        if mutation=='CE':pins['CE']=(0,)
                        if mutation=='clock':pins['C']=(data[1],)
                        if mutation=='INIT':pars['INIT']="1'b1"
                    cells['r'+str(i)+'_psdsp']=('FDRE',parameters('FDRE',pars.items()),pins)
                a=regs[-1:]*13+list(reversed(regs))
            pars={'AREG':'0' if changed else '1','ACASCREG':'0' if changed else '1','A_INPUT':'"DIRECT"','AMULTSEL':'"A"'}
            cells['dsp']=('DSP48E2',tuple(sorted(pars.items())),{'A':tuple(a),'CLK':(clock,),'CEA2':(0 if changed else 1,),'RSTA':(0,),'INMODE':(0,)*5,'P':(out,)})
            designs.append(SimpleNamespace(cells=cells,pool=pool,top_ports={'p':('output',(out,))},opaque=collections.Counter()))
        with tempfile.TemporaryDirectory() as work:return verify(*designs,Path(work))

    def test_dsp_register_pushout_preserves_all_sign_extended_bits(self):
        report=self.dsp_case()
        self.assertTrue(report['visible_structural_certificate_passed'])
        self.assertEqual(report['dsp_rewrites'][0]['unique_registers'],17)
        self.assertEqual(len(report['dsp_rewrites'][0]['bit_checks']),30)

    def test_wrong_dsp_bit_clock_enable_or_initial_state_is_rejected(self):
        for mutation in ('D','clock','CE','INIT'):
            with self.subTest(mutation=mutation):
                self.assertFalse(self.dsp_case(mutation)['visible_structural_certificate_passed'])


if __name__=='__main__':unittest.main()
