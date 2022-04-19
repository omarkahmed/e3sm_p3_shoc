
#include "micro_p3.h"
#include <fstream>      // std::ifstream

#define USE_SCREAM

using namespace scream;
using namespace scream::p3;

// initialize the lookup table for p3
// NOTES: this is done on CPU
void micro_p3_init_tables() {
  using P3F = Functions<Real, DefaultDevice>;

  YAKL_SCOPE( mu_r_table,    :: mu_r_table);
  YAKL_SCOPE( dnu_table,     :: dnu_table);
  YAKL_SCOPE( vn_table,      :: vn_table);
  YAKL_SCOPE( vm_table,      :: vm_table);
  YAKL_SCOPE( revap_table,   :: revap_table);
  YAKL_SCOPE( ice_table,     :: ice_table);
  YAKL_SCOPE( collect_table, :: collect_table);

  // host copy of table data
  realHost1d mu_r_table_h    = realHost1d( "mu_r_table_h "  , mu_r_table_dim);
  realHost2d vn_table_h      = realHost2d( "vn_table_h   "  , vtable_dim0, vtable_dim1);
  realHost2d vm_table_h      = realHost2d( "vm_table_h   "  , vtable_dim0, vtable_dim1);
  realHost2d revap_table_h   = realHost2d( "revap_table_h"  , vtable_dim0, vtable_dim1);
  realHost4d ice_table_h     = realHost4d( "ice_table_h"    , densize, rimsize, isize, ice_table_size);
  realHost5d collect_table_h = realHost5d( "collect_table_h", densize, rimsize, isize, rcollsize, collect_table_size);

  // local variables
  real mu_r, dm, lamr, dum1, dum2, dum3, dum4, dum5, dd, amg, dia, vt; 

  constexpr real piov6 = pi/6.0;
  constexpr real thrd  = 1./3.;
  constexpr real sxth  = 1./6.;

  parallel_for( 1 , YAKL_LAMBDA ( int i ) {
    dnu_table(0)  =  0.000;
    dnu_table(1)  = -0.557;
    dnu_table(2)  = -0.430;
    dnu_table(3)  = -0.307;
    dnu_table(4)  = -0.186;
    dnu_table(5)  = -0.067;
    dnu_table(6)  = -0.050;
    dnu_table(7)  = -0.167;
    dnu_table(8)  = -0.282;
    dnu_table(9)  = -0.397;
    dnu_table(10) = -0.512;
    dnu_table(11) = -0.626;
    dnu_table(12) = -0.739;
    dnu_table(13) = -0.853;
    dnu_table(14) = -0.966;
    dnu_table(15) = -0.966;
  });

#ifdef USE_SCREAM
  init_tables_from_f90_c(vn_table_h.myData, vm_table_h.myData, revap_table_h.myData, mu_r_table_h.myData);

#else
  parallel_for( 150 , YAKL_LAMBDA ( int i ) {
     mu_r_table(i) = mu_r_const;
  });

  for (auto ii = 0; ii < 10; ii++) {
     for (auto jj = 0; jj < 300; jj++) {

        mu_r = mu_r_const;

        if (jj < 20) {
           dm = ((real)(jj+1)*10.-5.)*1.e-6;      // mean size [m]
        } else if (jj >= 20) {
           dm = ((real)(jj-19)*30.+195.)*1.e-6; // mean size [m]
        }

        lamr = (mu_r+1.)/dm;

        // do numerical integration over PSD
        dum1 = 0.; // numerator,   number-weighted fallspeed
        dum2 = 0.; // denominator, number-weighted fallspeed
        dum3 = 0.; // numerator,   mass-weighted fallspeed
        dum4 = 0.; // denominator, mass-weighted fallspeed
        dum5 = 0.; // term for ventilation factor in evap
        dd   = 2.;

        // loop over PSD to numerically integrate number and mass-weighted mean fallspeeds
        for (auto kk = 0; kk < 10000; ++kk) {
           dia = ((real)(kk+1)*dd-dd*0.5)*1.e-6;  // size bin [m]
           amg = piov6*997000.*std::pow(dia, 3);  // mass [kg]

           //get fallspeed as a function of size [m s-1]
           if (dia*1.e+6 <= 134.43) {
              vt = 4.5795e+3*std::pow(amg, (2.*thrd));
           } else if (dia*1.e+6 < 1511.64) {
              vt = 4.962e+1*std::pow(amg, thrd);
           } else if (dia*1.e+6 < 3477.84) {
              vt = 1.732e+1*std::pow(amg, sxth);
           } else {
              vt = 9.17;
           }

           //note: factor of 4.*mu_r is non-answer changing and only needed to
           //      prevent underflow/overflow errors, same with 3.*mu_r for dum5
           dum1 = dum1 + vt*std::pow(10., (mu_r*std::log10(dia)+4.*mu_r)*std::exp(-lamr*dia)*dd*1.e-6);
           dum2 = dum2 + std::pow(10., (mu_r*std::log10(dia)+4.*mu_r)*std::exp(-lamr*dia)*dd*1.e-6);
           dum3 = dum3 + vt*std::pow(10., ((mu_r+3.)*std::log10(dia)+4.*mu_r)*std::exp(-lamr*dia)*dd*1.e-6);
           dum4 = dum4 + std::pow(10.,((mu_r+3.)*std::log10(dia)+4.*mu_r)*std::exp(-lamr*dia)*dd*1.e-6);
           dum5 = dum5 + std::pow((vt*dia), 0.5)*std::pow(10., ((mu_r+1.)*std::log10(dia)+3.*mu_r)*std::exp(-lamr*dia)*dd*1.e-6);

        } // kk-loop (over PSD)

        dum2 = max(dum2, 1.e-30);  //to prevent divide-by-zero below
        dum4 = max(dum4, 1.e-30);  //to prevent divide-by-zero below
        dum5 = max(dum5, 1.e-30);  //to prevent log10-of-zero below

        vn_table_h(jj,ii)    = dum1/dum2;
        vm_table_h(jj,ii)    = dum3/dum4;
        revap_table_h(jj,ii) = std::pow(10.,(std::log10(dum5)+(mu_r+1.)*std::log10(lamr)-(3.*mu_r)));
     }
  }
#endif

  //
  // read in ice microphysics table into host views
  //
  std::string filename = std::string(P3F::P3C::p3_lookup_base) + std::string(P3F::P3C::p3_version);
  std::ifstream infile(filename);

  // read header
  std::string version, version_val;
  infile >> version >> version_val;
  EKAT_REQUIRE_MSG(version == "VERSION", "Bad " << filename << ", expected VERSION X.Y.Z header");
  EKAT_REQUIRE_MSG(version_val == P3F::P3C::p3_version, "Bad " << filename << ", expected version " << P3F::P3C::p3_version << ", but got " << version_val);

  // read tables
  double dum_s; 
  int dum_i; // dum_s needs to be double to stream correctly
  for (int jj = 0; jj < densize; ++jj) {
    for (int ii = 0; ii < rimsize; ++ii) {
      for (int i = 0; i < isize; ++i) {
        infile >> dum_i >> dum_i;
        int j_idx = 0;
        for (int j = 0; j < 15; ++j) {
          infile >> dum_s;
          if (j > 1 && j != 10) {
            ice_table_h(jj, ii, i, j_idx++) = dum_s;
          }
        }
      }

      for (int i = 0; i < isize; ++i) {
        for (int j = 0; j < rcollsize; ++j) {
          infile >> dum_i >> dum_i;
          int k_idx = 0;
          for (int k = 0; k < 6; ++k) {
            infile >> dum_s;
            if (k == 3 || k == 4) {
              collect_table_h(jj, ii, i, j, k_idx++) = std::log10(dum_s);
            }
          }
        }
      }
    }
  }

 // update device tables
 vn_table_h     .deep_copy_to(vn_table);
 vm_table_h     .deep_copy_to(vm_table);
 revap_table_h  .deep_copy_to(revap_table);
 ice_table_h    .deep_copy_to(ice_table);
 collect_table_h.deep_copy_to(collect_table);
}

void micro_p3_diagnose() {
  YAKL_SCOPE( qv          , :: qv);
  YAKL_SCOPE( qcl         , :: qcl);
  YAKL_SCOPE( qci         , :: qci);
  YAKL_SCOPE( qpl         , :: qpl);
  YAKL_SCOPE( qpi         , :: qpi);
  YAKL_SCOPE( micro_field , :: micro_field);

  parallel_for( SimpleBounds<4>(nzm,ny,nx,ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
    qv(k,j,i,icrm)  = micro_field(idx_qt,k,j+offy_s,i+offx_s,icrm) - micro_field(idx_qc,k,j+offy_s,i+offx_s,icrm);
    qcl(k,j,i,icrm) = micro_field(idx_qc,k,j+offy_s,i+offx_s,icrm);
    qpl(k,j,i,icrm) = micro_field(idx_qr,k,j+offy_s,i+offx_s,icrm);
    qci(k,j,i,icrm) = micro_field(idx_qi,k,j+offy_s,i+offx_s,icrm);
    qpi(k,j,i,icrm) = 0.; // P3 doesn't have a "precipitating" ice category, so put it all as "cloud ice"
  });
}


void micro_p3_init() {
  YAKL_SCOPE( fluxbmk , ::fluxbmk);
  YAKL_SCOPE( fluxtmk , ::fluxtmk);
  YAKL_SCOPE( mkwle   , ::mkwle);
  YAKL_SCOPE( mkwsb   , ::mkwsb);
  YAKL_SCOPE( mkadv   , ::mkadv);
  YAKL_SCOPE( mkdiff  , ::mkdiff);
  YAKL_SCOPE( qpsrc   , ::qpsrc);
  YAKL_SCOPE( qpevp   , ::qpevp);
  YAKL_SCOPE( ncrms   , ::ncrms);

  // for (int l=0; l<nmicro_fields; k++) {
  //   for (int j=0; j<ny; j++) {
  //     for (int i=0; i<nx; i++) {
  //       for (int icrm=0; icrm<ncrms; icrm++) {
  parallel_for( SimpleBounds<4>(nmicro_fields,ny,nx,ncrms) , YAKL_LAMBDA (int l, int j, int i, int icrm) {
    fluxbmk(l,j,i,icrm) = 0.0;
    fluxtmk(l,j,i,icrm) = 0.0;
  });

  micro_p3_diagnose();

  // for (int l=0; l<nmicro_fields; k++) {
  //  for (int k=0; k<nz; k++) {
  //    for (int icrm=0; icrm<ncrms; icrm++) {
  parallel_for( SimpleBounds<3>(nmicro_fields,nz,ncrms) , YAKL_LAMBDA (int l, int k, int icrm) {
    mkwle (l,k,icrm) = 0.0;
    mkwsb (l,k,icrm) = 0.0;
    mkadv (l,k,icrm) = 0.0;
    mkdiff(l,k,icrm) = 0.0;
  });
  
  // for (int k=0; k<nz; k++) {
  //       for (int icrm=0; icrm<ncrms; icrm++) {
  parallel_for( SimpleBounds<2>(nz,ncrms) , YAKL_LAMBDA (int k, int icrm) {
    qpsrc(k,icrm) = 0.0;
    qpevp(k,icrm) = 0.0;
  });

  // initialize p3 tables here
  micro_p3_init_tables();
}


void get_cloud_fraction(int its, int ite, int kts, int kte, real2d& cloud_frac,
                       real2d& qc, real2d& qr, real2d& qi, std::string& method,
                       real2d& cld_frac_i, real2d& cld_frac_l, real2d& cld_frac_r)
{
  // Temporary method for initial P3 implementation

  // old comment for "cldm" from EAM = "mean cloud fraction over the time step"

  // int nk = kte-kts+1;
  // int ni = ite-its+1;
  // parallel_for( SimpleBounds<2>(ni,nk) , YAKL_LAMBDA (int i, int k) {
  //   cld_frac_i(i,k) = 1.0; //max(cloud_frac(i,k), 0);
  //   cld_frac_l(i,k) = 1.0; //max(cloud_frac(i,k), 0);
  //   cld_frac_r(i,k) = 1.0; //cldm(i,k);
  // });

  real cld_water_threshold = 0.001;
  parallel_for( SimpleBounds<4>(nzm,ny,nx,ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
    int icol = i+nx*(j+icrm*ny);
    cld_frac_l(icol,k) = 1.0;
    cld_frac_i(icol,k) = 1.0;
    cld_frac_r(icol,k) = 1.0;
    // cld_frac_l(icol,k) = p3_mincld;
    // cld_frac_i(icol,k) = p3_mincld;
    // cld_frac_r(icol,k) = p3_mincld;
    // real cld_water_tmp = rho(k,icrm)*adz(k,icrm)*dz(icrm)*(qcl(k,j,i,icrm)+qci(k,j,i,icrm)+qpl(k,j,i,icrm));
    // if(cld_water_tmp > cld_water_threshold) {
    //   if (qcl(k,j,i,icrm)>0) { cld_frac_l(icol,k) = 1.0; }
    //   if (qci(k,j,i,icrm)>0) { cld_frac_i(icol,k) = 1.0; }
    //   if (qpl(k,j,i,icrm)>0) { cld_frac_r(icol,k) = 1.0; }
    // }
  });

#if 0
  if (method == "in_cloud") {

     parallel_for( SimpleBounds<2>(ni,nk) , YAKL_LAMBDA (int i, int k) {
       // in_cloud means that precip_frac (cld_frac_r) = cloud (cldm) frac when cloud mass
       // is present. Below cloud, precip frac is equal to the cloud
       // fraction from the last layer that had cloud. Since presence or
       // absence of cloud is defined as mass > qsmall, sub-cloud precip
       // frac for the in_cloud method tends to be very small and is
       // very sensitive to tiny changes in condensate near cloud base.
       if (qc(i,k) < p3_qsmall && qi(i,k) < p3_qsmall && k > 0) {
         // max(cld_frac_r above and cld_frac_r for this layer) is taken here
         // because code is incapable of handling cld_frac_r<cldm for a
         // given grid cell
         cld_frac_r(i,k) = max(cld_frac_r(i,k-1),cld_frac_r(i,k));
       }
     });

  } else if (method == "max_overlap") {
      // max overlap is the max cloud fraction in all layers above which are
      // connected to this one by a continuous band of precip mass. If
      // there's no precip mass falling into a cell, it's precip frac is equal
      // to the cloud frac, which is probably ~zero.

      // IF rain or ice mix ratios are smaller than threshold,
      // then leave cld_frac_r as cloud fraction at current level

      parallel_for( SimpleBounds<2>(ni,nk) , YAKL_LAMBDA (int i, int k) {
        if (k > 0) {
           if (qr(i,k-1) >= p3_qsmall || qi(i,k-1) >= p3_qsmall) {
              cld_frac_r(i,k) = max(cld_frac_r(i,k-1),cld_frac_r(i,k));
           }
        }
      });
  }
#endif

}

// August-Roche-Magnus formula for saturation vapor pressure 
// https://en.wikipedia.org/wiki/Clausius%E2%80%93Clapeyron_relation#August%E2%80%93Roche%E2%80%93Magnus_formula
YAKL_INLINE real saturation_vapor_pressure(real tabs) {
  real tc = tabs - 273.15;                        // input temperature is K, so convert to Celsius
  return 610.94 * exp( 17.625*tc / (243.04+tc) ); // return sat vapor pressure in Pa
}
YAKL_INLINE real saturation_specific_humidity(real tabs, real pressure) {
  real esat = saturation_vapor_pressure( tabs ); // get sat vapor pressure in Pa
  real wsat = 0.622*esat / (pressure - esat);    // saturation mixing ratio
  real qsat = wsat/(1+wsat);                     // convert sat mixing ratio to sat specific humidity
  return qsat;
}

// Compute an instantaneous adjustment of sub or super saturation
YAKL_INLINE void compute_adjusted_state( real &qt_in, real &qc_in, real &qv_in, real &tabs_in, real &pres_in ) {
  // Define a tolerance and max iterations for convergence
  real tol = 1.e-6;
  real min_qv = 1e-10;
  real min_qc = 0;
  int max_iteration = 10;
  int iteration_cnt = 0;

  // Saturation specific humidity
  real qsat = saturation_specific_humidity(tabs_in,pres_in);

  // set initial vapor from input total water and cloud water
  qv_in = qt_in - qc_in; 

  // local variables for iteration
  real tabs_loc = tabs_in;
  real qv_loc = qv_in; 
  real qc_loc = qc_in;
  bool keep_iterating = true;

  // If we're super-saturated, we need to condense
  if (qv_in > qsat) {
    // Set bounds on how much mass to condense
    real cond1 = 0;   // Minimum amount we can condense
    real cond2 = qv_in;  // Maximum amount we can condense
    while (keep_iterating) {
      // How much water vapor to condense for this iteration
      real cond = (cond1 + cond2) / 2;
      // update vapor and cloud condensate
      qv_loc = qv_in - cond;
      qc_loc = qc_in + cond;
      // check if values are under threshold
      if (qv_loc<min_qv) { qc_loc = qc_loc + (qv_loc-min_qv); qv_loc = min_qv; }
      if (qc_loc<min_qc) { qv_loc = qv_loc + (qc_loc-min_qc); qc_loc = min_qc; }
      // update temperature
      tabs_loc = tabs_in + fac_cond*cond;
      // update saturation value
      real qsat = saturation_specific_humidity(tabs_loc,pres_in);
      // If still supersaturated condense more, otherwise condense less
      if (qv_loc> qsat) { cond1 = cond; }
      if (qv_loc<=qsat) { cond2 = cond; }
      // If we've converged or reached max iteration, then stop iterating
      iteration_cnt++;
      if ( abs(cond2-cond1)<=tol || iteration_cnt>=max_iteration) {
        qv_in = qv_loc;
        qc_in = qc_loc;
        tabs_in = tabs_loc;
        keep_iterating = false;
      }
    }
  } else if (qv_in < qsat && qc_in > 0) {
    // If we are unsaturated and have cloud condensate, we need to evaporate
    // Set bounds on how much mass to evaporate
    real evap1 = 0;  // minimum amount we can evaporate
    real evap2 = qc_in; // maximum amount we can evaporate
    while (keep_iterating) {
      // How much water vapor to evaporate for this iteration
      real evap = (evap1 + evap2) / 2;
      // update vapor and cloud condensate
      qv_loc = qv_in + evap;
      qc_loc = qc_in - evap;
      // check if values are under threshold
      if (qv_loc<min_qv) { qc_loc = qc_loc + (qv_loc-min_qv); qv_loc = min_qv; }
      if (qc_loc<min_qc) { qv_loc = qv_loc + (qc_loc-min_qc); qc_loc = min_qc; }
      // update temperature
      tabs_loc = tabs_in - fac_cond*evap;
      // update saturation value
      real qsat = saturation_specific_humidity(tabs_loc,pres_in);
      // If still unsaturated evaporate more, otherwise evaporate less
      if (qv_loc< qsat) { evap1 = qc_in; }
      if (qv_loc>=qsat) { evap2 = qc_in; }
      // If we've converged or reached max iteration, then stop iterating
      iteration_cnt++;
      if ( abs(evap2-evap1)<=tol || iteration_cnt>=max_iteration) {
        qv_in = qv_loc;
        qc_in = qc_loc;
        tabs_in = tabs_loc;
        keep_iterating = false;
      }
    }
  }

  // Update total water
  qt_in = qv_in + qc_in;
}


//
// main micro_p3 microproc
//
void micro_p3_proc() {
  using P3F        = Functions<Real, DefaultDevice>;
  using KT         = typename P3F::KT;
  using ExeSpace   = typename KT::ExeSpace;
  using MemberType = typename P3F::MemberType;
  using Spack      = typename P3F::Spack;
  using view_1d    = typename P3F::view_1d<Spack>;
  using uview_1d   = typename P3F::uview_1d<Spack>;
  using view_2d    = typename P3F::view_2d<Spack>;
  using sview_1d   = typename P3F::view_1d<Real>;
  using sview_2d   = typename P3F::view_2d<Real>;

  YAKL_SCOPE( micro_field        , :: micro_field);
  YAKL_SCOPE( qv                 , :: qv);
  YAKL_SCOPE( qcl                , :: qcl);
  YAKL_SCOPE( qci                , :: qci);
  YAKL_SCOPE( qpl                , :: qpl);
  YAKL_SCOPE( qpi                , :: qpi);
  YAKL_SCOPE( dt                 , :: dt);
  YAKL_SCOPE( t                  , :: t);
  YAKL_SCOPE( gamaz              , :: gamaz);
  YAKL_SCOPE( tabs               , :: tabs);
  YAKL_SCOPE( ncrms              , :: ncrms);
  YAKL_SCOPE( dz                 , :: dz);
  YAKL_SCOPE( adz                , :: adz);
  YAKL_SCOPE( pdel               , :: pdel);
  YAKL_SCOPE( pres               , :: pres);
  YAKL_SCOPE( longitude0         , :: longitude0);
  YAKL_SCOPE( latitude0          , :: latitude0);
  YAKL_SCOPE( z0                 , :: z0);
  YAKL_SCOPE( nc_nuceat_tend     , :: nc_nuceat_tend);
  YAKL_SCOPE( nccn               , :: nccn);
  YAKL_SCOPE( ni_activated       , :: ni_activated);
  YAKL_SCOPE( diag_eff_radius_qc , :: diag_eff_radius_qc);
  YAKL_SCOPE( diag_eff_radius_qi , :: diag_eff_radius_qi);
  YAKL_SCOPE( t_prev             , :: t_prev);
  YAKL_SCOPE( q_prev             , :: q_prev);

  // p3 tables
  YAKL_SCOPE( mu_r_table,    :: mu_r_table);
  YAKL_SCOPE( dnu_table,     :: dnu_table);
  YAKL_SCOPE( vn_table,      :: vn_table);
  YAKL_SCOPE( vm_table,      :: vm_table);
  YAKL_SCOPE( revap_table,   :: revap_table);
  YAKL_SCOPE( ice_table,     :: ice_table);
  YAKL_SCOPE( collect_table, :: collect_table);

  // output 
  YAKL_SCOPE( qv2qi_depos_tend   , :: qv2qi_depos_tend);
  YAKL_SCOPE( rho_qi             , :: rho_qi);
  YAKL_SCOPE( precip_liq_flux    , :: precip_liq_flux);
  YAKL_SCOPE( precip_ice_flux    , :: precip_ice_flux);

  YAKL_SCOPE( liq_ice_exchange,  :: liq_ice_exchange);
  YAKL_SCOPE( vap_liq_exchange,  :: vap_liq_exchange);
  YAKL_SCOPE( vap_ice_exchange,  :: vap_ice_exchange);

  // output
  YAKL_SCOPE( precsfc           , :: precsfc);
  YAKL_SCOPE( precssfc          , :: precssfc);
  YAKL_SCOPE( CF3D              , :: CF3D);

  const int nlev  = nzm;
  const int ncol  = ncrms*nx*ny;
  const int npack = ekat::npack<Spack>(nlev);

  real2d qc_in("qc",ncol, nlev);
  real2d nc_in("nc",ncol, nlev);
  real2d qr_in("qr",ncol, nlev);
  real2d nr_in("nr",ncol, nlev);
  real2d qi_in("qi",ncol, nlev);
  real2d qm_in("qm",ncol, nlev);
  real2d ni_in("ni",ncol, nlev);
  real2d bm_in("bm",ncol, nlev);
  real2d qv_in("qv",ncol, nlev);
  real2d th_in("th",ncol, nlev);

  real2d nc_nuceat_tend_in("nuceat",ncol, nlev);
  real2d nccn_in("nccn",ncol, nlev);
  real2d ni_activated_in("ni_act",ncol, nlev);
  real2d inv_qc_relvar_in("inv_qc",ncol, nlev); // relative cloud water variance - not needed - set to 1
  real2d cld_frac_i_in("cld_frac_i",ncol, nlev);
  real2d cld_frac_l_in("cld_frac_l",ncol, nlev);
  real2d cld_frac_r_in("cld_frac_r",ncol, nlev);
  real2d dz_in("dz", ncol, nlev);
  real2d pmid_in("pmid", ncol, nlev);
  real2d pdel_in("pdel",ncol, nlev);
  real2d inv_exner_in("inv_exner",ncol, nlev);
  real2d q_prev_in("q_prev",ncol, nlev);
  real2d t_prev_in("t_prev",ncol, nlev);

  real2d cloud_frac_in("cloud_frac_in",ncol, nlev);

  // p3 output variables 
  // real2d qv2qi_depos_tend_in("qv2qi",ncol, nlev);
  // real2d precip_liq_surf_in("precip_liq",ncol, nlev);
  // real2d precip_ice_surf_in("precip_ice",ncol, nlev);
  // real2d diag_eff_radius_qc_in("diag_eff_qc",ncol, nlev);
  // real2d diag_eff_radius_qi_in("diag_eff_qi",ncol, nlev);
  // real2d rho_qi_in("rho_qi",ncol, nlev);
  // real2d precip_liq_flux_in("precip_liq_flux",ncol, nlev);
  // real2d precip_ice_flux_in("precip_ice_flux",ncol, nlev);

  // p3 output variables
  // real2d qv2qi_depos_tend_out("qv2qi_depos_tend", ncol, nlev);
  // real2d diag_eff_radius_qc_out("diag_eff_radius_qc", ncol, nlev);
  // real2d diag_eff_radius_qi_out("diag_eff_radius_qi", ncol, nlev);
  // real2d rho_qi_out("rho_qi", nzm, nx);
  // real2d precip_liq_flux_out("precip_liq_flux", ncol, nlev);
  // real2d precip_ice_flux_out("precip_ice_flux", ncol, nlev);

  // real1d precip_liq_surf_out("precip_liq_surf_d", ncol);
  // real1d precip_ice_surf_out("precip_ice_surf_d", ncol);

  //----------------------------------------------------------------------------
  // Populate P3 thermodynamic state
  //----------------------------------------------------------------------------
  micro_p3_diagnose();

  // Saturation adjustment - without SHOC we need to do a saturation adjustment and set qc
  if (turbulence_scheme == turbulence::smag) {
    parallel_for( SimpleBounds<4>(nzm, ny, nx, ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
      tabs(k,j,i,icrm) = t(k,j+offy_s,i+offx_s,icrm) - gamaz(k,icrm)
                      + fac_cond *( qcl(k,j,i,icrm) + qpl(k,j,i,icrm) ) 
                      + fac_sub  *( qci(k,j,i,icrm) + qpi(k,j,i,icrm) );
      real tmp_pres = pres(k,icrm)*100.;
      compute_adjusted_state( micro_field(idx_qt,k,j+offy_s,i+offx_s,icrm), 
                              micro_field(idx_qc,k,j+offy_s,i+offx_s,icrm), 
                              qv(k,j,i,icrm), tabs(k,j,i,icrm), tmp_pres );
    });
    // update diagnostic fields
    micro_p3_diagnose();
    // update liq static energy - do we need this? LSE should be conserved for saturation adjustment
    parallel_for( SimpleBounds<4>(nzm, ny, nx, ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
      t(k,j+offy_s,i+offx_s,icrm) = tabs(k,j,i,icrm) + gamaz(k,icrm)
                    - fac_cond *( qcl(k,j,i,icrm) + qpl(k,j,i,icrm) )
                    - fac_sub  *( qci(k,j,i,icrm) + qpi(k,j,i,icrm) );
    });
  }

  // Diagnose potential temperature
  parallel_for( SimpleBounds<4>(nzm, ny, nx, ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
    int icol = i+nx*(j+ny*icrm);
    int ilev = k;
    inv_exner_in(icol, ilev) = 1./std::pow((pres(k,icrm)*1.0e-3), (rgas/cp));
    tabs(k,j,i,icrm) = t(k,j+offy_s,i+offx_s,icrm) - gamaz(k,icrm)
                      + fac_cond *( qcl(k,j,i,icrm) + qpl(k,j,i,icrm) ) 
                      + fac_sub  *( qci(k,j,i,icrm) + qpi(k,j,i,icrm) );
    th_in(icol, ilev) = tabs(k,j,i,icrm)*inv_exner_in(icol, ilev);
  });

// auto fp=fopen("q.txt","w");
  parallel_for( SimpleBounds<4>(nzm, ny, nx, ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
    int icol = i+nx*(j+ny*icrm);
    int ilev = k;
    qv_in(icol,ilev) = micro_field(idx_qt,k,j+offy_s,i+offx_s,icrm) - micro_field(idx_qc,k,j+offy_s,i+offx_s,icrm);
    qc_in(icol,ilev) = micro_field(idx_qc,k,j+offy_s,i+offx_s,icrm); 
    nc_in(icol,ilev) = micro_field(idx_nc,k,j+offy_s,i+offx_s,icrm);
    qr_in(icol,ilev) = micro_field(idx_qr,k,j+offy_s,i+offx_s,icrm);
    nr_in(icol,ilev) = micro_field(idx_nr,k,j+offy_s,i+offx_s,icrm);
    qi_in(icol,ilev) = micro_field(idx_qi,k,j+offy_s,i+offx_s,icrm);
    qm_in(icol,ilev) = micro_field(idx_qm,k,j+offy_s,i+offx_s,icrm);
    ni_in(icol,ilev) = micro_field(idx_ni,k,j+offy_s,i+offx_s,icrm);
    bm_in(icol,ilev) = micro_field(idx_bm,k,j+offy_s,i+offx_s,icrm);

// fprintf(fp,"%d, %d, %d, %d %13.6e, %13.6e, %13.6e, %13.6e, %13.6e, %13.6e, %13.6e, %13.6e, %13.6e\n",k,j,i,icrm,
// qv_in(icol,ilev),qc_in(icol,ilev),nc_in(icol,ilev),qr_in(icol,ilev),nr_in(icol,ilev),qi_in(icol,ilev),qm_in(icol,ilev),ni_in(icol,ilev),bm_in(icol,ilev));

  });
// fclose(fp);

  view_2d qv_d("qv", ncol, npack),
          qc_d("qc", ncol, npack),
          nc_d("nc", ncol, npack),
          qr_d("qr", ncol, npack),
          nr_d("nr", ncol, npack),
          qi_d("qi", ncol, npack),
          qm_d("qm", ncol, npack),
          ni_d("ni", ncol, npack),
          bm_d("bm", ncol, npack),
          th_d("th", ncol, npack);

  array_to_view(qc_in.myData, ncol, nlev, qc_d);
  array_to_view(nc_in.myData, ncol, nlev, nc_d);
  array_to_view(qr_in.myData, ncol, nlev, qr_d);
  array_to_view(nr_in.myData, ncol, nlev, nr_d);
  array_to_view(qi_in.myData, ncol, nlev, qi_d);
  array_to_view(qm_in.myData, ncol, nlev, qm_d);
  array_to_view(ni_in.myData, ncol, nlev, ni_d);
  array_to_view(bm_in.myData, ncol, nlev, bm_d);
  array_to_view(qv_in.myData, ncol, nlev, qv_d);
  array_to_view(th_in.myData, ncol, nlev, th_d);
   
  P3F::P3PrognosticState prog_state{qc_d, nc_d, qr_d, nr_d, qi_d, qm_d,
                                    ni_d, bm_d, qv_d, th_d};

  //----------------------------------------------------------------------------
  // Populate P3 diagnostic inputs
  //----------------------------------------------------------------------------
  parallel_for( SimpleBounds<4>(nzm, ny, nx, ncrms) , YAKL_LAMBDA (int k, int j, int i, int icrm) {
    int icol = i+nx*(j+icrm*ny);
    int ilev = k;
    nccn_in(icol,ilev)            = nccn(k,icrm);
    nc_nuceat_tend_in(icol,ilev)  = nc_nuceat_tend(k,icrm);
    ni_activated_in(icol,ilev)    = ni_activated(k,icrm);
    inv_qc_relvar_in(icol,ilev)   = 1.; // not needed - set to 1
    dz_in(icol,ilev)              = adz(k,icrm)*dz(icrm);
    pmid_in(icol,ilev)            = pres(k,icrm)*100.;
    pdel_in(icol,ilev)            = pdel(k,icrm)*100.;
    cloud_frac_in(icol,ilev)      = CF3D(k,j,i,icrm);
    q_prev_in(icol,ilev)          = q_prev(k,j,i,icrm);
    t_prev_in(icol,ilev)          = t_prev(k,j,i,icrm);
  });

  std::string method("in_cloud");
  get_cloud_fraction(0, ncol-1, 0, nlev-1, cloud_frac_in, qc_in, qr_in, qi_in, method,
                     cld_frac_i_in, cld_frac_l_in, cld_frac_r_in);

  view_2d nc_nuceat_tend_d("nc_nuceat_tend", ncol, npack),
          nccn_d("nccn", ncol, npack),
          ni_activated_d("ni_activated", ncol, npack),
          inv_qc_relvar_d("inv_qc_relvar", ncol, npack),
          dz_d("dz", ncol, npack),
          pmid_d("pmid", ncol, npack),
          pdel_d("pdel", ncol, npack),
          inv_exner_d("inv_exner", ncol, npack),
          t_prev_d("t_prev", ncol, npack),
          q_prev_d("q_prev", ncol, npack),
          cld_frac_i_d("cld_frac_i", ncol, npack),
          cld_frac_l_d("cld_frac_l", ncol, npack),
          cld_frac_r_d("cld_frac_r", ncol, npack);

  array_to_view(nc_nuceat_tend_in.myData, ncol, nlev, nc_nuceat_tend_d);
  array_to_view(nccn_in.myData, ncol, nlev, nccn_d);
  array_to_view(ni_activated_in.myData, ncol, nlev, ni_activated_d);
  array_to_view(inv_qc_relvar_in.myData, ncol, nlev, inv_qc_relvar_d);
  array_to_view(dz_in.myData, ncol, nlev, dz_d);
  array_to_view(pmid_in.myData, ncol, nlev, pmid_d);
  array_to_view(pdel_in.myData, ncol, nlev, pdel_d);
  array_to_view(inv_exner_in.myData, ncol, nlev, inv_exner_d);
  array_to_view(t_prev_in.myData, ncol, nlev, t_prev_d);
  array_to_view(q_prev_in.myData, ncol, nlev, q_prev_d);
  array_to_view(cld_frac_i_in.myData, ncol, nlev, cld_frac_i_d);
  array_to_view(cld_frac_l_in.myData, ncol, nlev, cld_frac_l_d);
  array_to_view(cld_frac_r_in.myData, ncol, nlev, cld_frac_r_d);

  P3F::P3DiagnosticInputs diag_inputs{nc_nuceat_tend_d, nccn_d, 
                                      ni_activated_d, inv_qc_relvar_d, 
                                      cld_frac_i_d, cld_frac_l_d, cld_frac_r_d, 
                                      pmid_d, dz_d, pdel_d, inv_exner_d, 
                                      q_prev_d, t_prev_d};

  //----------------------------------------------------------------------------
  // Populate P3 diagnostic outputs
  //----------------------------------------------------------------------------
  view_2d qv2qi_depos_tend_d("qv2qi_depos_tend", ncol, npack),
          diag_eff_radius_qc_d("diag_eff_radius_qc", ncol, npack),
          diag_eff_radius_qi_d("diag_eff_radius_qi", ncol, npack),
          rho_qi_d("rho_qi", ncol, npack),
          precip_liq_flux_d("precip_liq_flux", ncol, npack),
          precip_ice_flux_d("precip_ice_flux", ncol, npack);

  sview_1d precip_liq_surf_d("precip_liq_surf_d", ncol), 
           precip_ice_surf_d("precip_ice_surf_d", ncol);

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {ncol, npack, Spack::n}), KOKKOS_LAMBDA(int icol, int ilev, int s) {
     qv2qi_depos_tend_d(icol,ilev)[s] = 0.;
     diag_eff_radius_qc_d(icol,ilev)[s] = 0.;
     diag_eff_radius_qi_d(icol,ilev)[s] = 0.;
     rho_qi_d(icol,ilev)[s] = 0.;
     precip_liq_flux_d(icol,ilev)[s] = 0.;
     precip_ice_flux_d(icol,ilev)[s] = 0.;
  });

  Kokkos::parallel_for("precip", ncol, KOKKOS_LAMBDA (const int& icol) {
    precip_liq_surf_d(icol) = 0.;
    precip_ice_surf_d(icol) = 0.;
  });

  P3F::P3DiagnosticOutputs diag_outputs {qv2qi_depos_tend_d, precip_liq_surf_d,
                                         precip_ice_surf_d, diag_eff_radius_qc_d, diag_eff_radius_qi_d,
                                         rho_qi_d,precip_liq_flux_d, precip_ice_flux_d};

  //----------------------------------------------------------------------------
  // Populate P3 infrastructure
  //----------------------------------------------------------------------------
  int it{1};
  int its{0};
  int ite{ncrms*crm_nx*crm_ny};
  int kts{0}; 
  int kte{nzm}; 
  bool do_predict_nc, do_prescribed_CCN;
  sview_2d col_location_d("col_location_d", ncol, 3);

  do_predict_nc = true;
  do_prescribed_CCN = false;

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<4>>({0, 0, 0, 0}, {nzm, ny, nx, ncrms}), KOKKOS_LAMBDA(int k, int j, int i, int icrm) {
     int icol = i+nx*(j+icrm*ny);
     col_location_d(icol, 0) = z0.myData[icrm];
     col_location_d(icol, 1) = longitude0.myData[icrm];
     col_location_d(icol, 2) = latitude0.myData[icrm];
  });

  P3F::P3Infrastructure infrastructure{dt, it, its, ite, kts, kte,
                                       do_predict_nc, do_prescribed_CCN, col_location_d};

  //----------------------------------------------------------------------------
  // Populate P3 history output
  //----------------------------------------------------------------------------
  view_2d liq_ice_exchange_d("liq_ice_exchange_d", ncol, npack),
          vap_liq_exchange_d("vap_liq_exchange_d", ncol, npack),
          vap_ice_exchange_d("vap_ice_exchange_d", ncol, npack);

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0, 0}, {ncol, npack}), KOKKOS_LAMBDA(int icol, int ilev) {
     liq_ice_exchange_d(icol,ilev) = 0.;
     vap_liq_exchange_d(icol,ilev) = 0.;
     vap_ice_exchange_d(icol,ilev) = 0.;
  });

  P3F::P3HistoryOnly history_only {liq_ice_exchange_d, vap_liq_exchange_d,
                                   vap_ice_exchange_d};

  using mutable1d    = typename KT::template view<Real*>;
  using dnutable1d   = typename KT::template view<Real*>;
  using vtable2d     = typename KT::template view<Real**>;
  using icetable     = typename KT::template view<Real****>;
  using collecttable = typename KT::template view<Real*****>;

  // 1d tables
  //P3F::view_1d_table mu_r_table_vals;
  mutable1d mu_r_table_vals("mu_r_table",C::MU_R_TABLE_DIM);

  // dnu tables
  //P3F::view_dnu_table dnu;
  dnutable1d dnu("dun_table",dnusize);

  // 2d tables
  //P3F::view_2d_table vn_table_vals, vm_table_vals, revap_table_vals;
  vtable2d vn_table_vals("vn_table",vtable_dim0,vtable_dim1);
  vtable2d vm_table_vals("vm_table",vtable_dim0,vtable_dim1); 
  vtable2d revap_table_vals("revap_table",vtable_dim0,vtable_dim1);

  // ice tables
  //P3F::view_ice_table ice_table_vals;
  icetable ice_table_vals("ice_table",densize,rimsize,isize,ice_table_size);

  // collect tables
  //P3F::view_collect_table collect_table_vals;
  collecttable collect_table_vals("collect_table",densize,rimsize,isize,rcollsize,collect_table_size);

  Kokkos::parallel_for("mu_r_table", mu_r_table_dim, KOKKOS_LAMBDA (int i) {
     mu_r_table_vals(i) = mu_r_table(i);
  });

  Kokkos::parallel_for("dnu_table", dnusize, KOKKOS_LAMBDA (int i) {
     dnu(i) = dnu_table(i);
  });

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<2>>({0, 0}, {vtable_dim0, vtable_dim1}), KOKKOS_LAMBDA(int i, int j) {
     vn_table_vals(i,j)    = vn_table(i,j);
     vm_table_vals(i,j)    = vm_table(i,j);
     revap_table_vals(i,j) = revap_table(i,j);
  });

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<4>>({0, 0, 0, 0}, {densize, rimsize, isize, ice_table_size}), KOKKOS_LAMBDA(int i, int j, int k, int itab) {
     ice_table_vals(i,j,k,itab) = ice_table(i,j,k,itab);
  });

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<5>>({0, 0, 0, 0, 0}, {densize, rimsize, isize, rcollsize, collect_table_size}), 
                      KOKKOS_LAMBDA(int i, int j, int k, int r, int itab) {
     collect_table_vals(i,j,k,r,itab) = collect_table(i,j,k,r,itab);
  });

  P3F::P3LookupTables tables {mu_r_table_vals, 
                              vn_table_vals, 
                              vm_table_vals, 
                              revap_table_vals, 
                              ice_table_vals, 
                              collect_table_vals, 
                              dnu};

  //----------------------------------------------------------------------------
  // Call p3_main
  //----------------------------------------------------------------------------
  const int nlev_pack = ekat::npack<Spack>(nlev);
  const auto policy = ekat::ExeSpaceUtils<KT::ExeSpace>::get_default_team_policy(ncol, nlev_pack);
  ekat::WorkspaceManager<Spack, KT::Device> workspace_mgr(nlev_pack, 52, policy);

  auto elapsed_time = P3F::p3_main(prog_state, diag_inputs, diag_outputs, infrastructure,
                                   history_only, tables, workspace_mgr, ncol, nlev);

//printf("p3_main wall time: nj=%d, nk=%d, time=%13.6e\n", ite, kte, (float)elapsed_time*1.e-6);

  //----------------------------------------------------------------------------
  Kokkos::parallel_for("precip", ncol, KOKKOS_LAMBDA (const int& icol) {
    int i    = icol%nx;
    int j    = (icol/nx)%ny;
    int icrm = (icol/nx)/ny;
    auto precsfc_tmp  = precsfc (j,i,icrm);
    auto precssfc_tmp = precssfc(j,i,icrm);
    precsfc(j,i,icrm) = precsfc_tmp +(diag_outputs.precip_liq_surf(icol)
                                     +diag_outputs.precip_ice_surf(icol)) * 1000.0 * dt / dz(icrm);
    precssfc(j,i,icrm)= precssfc_tmp+(diag_outputs.precip_ice_surf(icol)) * 1000.0 * dt / dz(icrm);
  });

  // update microfield
  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {ncol, npack, Spack::n}), KOKKOS_LAMBDA(int icol, int ilev, int s) {
    int i    = icol%nx;
    int j    = (icol/nx)%ny;
    int icrm = (icol/nx)/ny;
    int k    = ilev*Spack::n + s;
    if (k < nlev) {
      micro_field(idx_qt,k,j+offy_s,i+offx_s,icrm) = prog_state.qv(icol,ilev)[s] + prog_state.qc(icol,ilev)[s];
      micro_field(idx_qc,k,j+offy_s,i+offx_s,icrm) = prog_state.qc(icol,ilev)[s];
      micro_field(idx_nc,k,j+offy_s,i+offx_s,icrm) = prog_state.nc(icol,ilev)[s];
      micro_field(idx_qr,k,j+offy_s,i+offx_s,icrm) = prog_state.qr(icol,ilev)[s];
      micro_field(idx_nr,k,j+offy_s,i+offx_s,icrm) = prog_state.nr(icol,ilev)[s];
      micro_field(idx_qi,k,j+offy_s,i+offx_s,icrm) = prog_state.qi(icol,ilev)[s];
      micro_field(idx_qm,k,j+offy_s,i+offx_s,icrm) = prog_state.qm(icol,ilev)[s];
      micro_field(idx_ni,k,j+offy_s,i+offx_s,icrm) = prog_state.ni(icol,ilev)[s];
      micro_field(idx_bm,k,j+offy_s,i+offx_s,icrm) = prog_state.bm(icol,ilev)[s];
    }
  });

  micro_p3_diagnose();

  // update LSE, temperature, and previous t/q
  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {ncol, npack, Spack::n}), KOKKOS_LAMBDA(int icol, int ilev, int s) {
    int i    = icol%nx;
    int j    = (icol/nx)%ny;
    int icrm = (icol/nx)/ny;
    int k    = ilev*Spack::n + s;
    if (k < nlev) {
      tabs(k,j,i,icrm) = prog_state.th(icol,ilev)[s]/inv_exner_in(icol,ilev);
      t(k,j+offy_s,i+offx_s,icrm) = tabs(k,j,i,icrm) + gamaz(k,icrm)
                    - fac_cond *( qcl(k,j,i,icrm) + qpl(k,j,i,icrm) )
                    - fac_sub  *( qci(k,j,i,icrm) + qpi(k,j,i,icrm) );
      t_prev(k,j,i,icrm) = tabs(k,j,i,icrm);
      q_prev(k,j,i,icrm) = qv(k,j,i,icrm);
    }
  });

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {ncol, npack, Spack::n}), KOKKOS_LAMBDA(int icol, int ilev, int s) {
    int i    = icol%nx;
    int j    = (icol/nx)%ny;
    int icrm = (icol/nx)/ny;
    int k    = ilev*Spack::n + s;
    if (k < nlev) {
      qv2qi_depos_tend(k,icrm)   = diag_outputs.qv2qi_depos_tend(icol,ilev)[s];
      diag_eff_radius_qc(k,icrm) = diag_outputs.diag_eff_radius_qc(icol,ilev)[s];
      diag_eff_radius_qi(k,icrm) = diag_outputs.diag_eff_radius_qi(icol,ilev)[s];
      rho_qi(k,icrm)             = diag_outputs.rho_qi(icol,ilev)[s];
      precip_liq_flux(k,icrm)    = diag_outputs.precip_liq_flux(icol,ilev)[s];
      precip_ice_flux(k,icrm)    = diag_outputs.precip_ice_flux(icol,ilev)[s];
    }
  });

  Kokkos::parallel_for(Kokkos::MDRangePolicy<Kokkos::Rank<3>>({0, 0, 0}, {ncol, npack, Spack::n}), KOKKOS_LAMBDA(int icol, int ilev, int s) {
    int i    = icol%nx;
    int j    = (icol/nx)%ny;
    int icrm = (icol/nx)/ny;
    int k    = ilev*Spack::n + s;
    if (k < nlev) {
      liq_ice_exchange(k,icrm) = history_only.liq_ice_exchange(icol,ilev)[s];
      vap_liq_exchange(k,icrm) = history_only.vap_liq_exchange(icol,ilev)[s];
      vap_ice_exchange(k,icrm) = history_only.vap_ice_exchange(icol,ilev)[s];
    }
  });

}

