/* Copyright (c) 2017-2019 Daan Christiaens
 *
 * MRtrix and this add-on module are distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without
 * even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 */

#include "command.h"
#include "image.h"
#include "thread_queue.h"
#include "dwi/gradient.h"

#include "dwi/svr/register.h"
#include "dwi/svr/psf.h"

#define DEFAULT_SSPW 1.0f


using namespace MR;
using namespace App;


void usage ()
{
  AUTHOR = "Daan Christiaens (daan.christiaens@kcl.ac.uk)";

  SYNOPSIS = "Register multi-shell spherical harmonics image to DWI slices or volumes.";

  DESCRIPTION
  + "This command takes DWI data and a multi-shell spherical harmonics (MSSH) signal "
    "prediction to estimate subject motion parameters with volume-to-slice registration.";

  ARGUMENTS
  + Argument ("data", "the input DWI data.").type_image_in()

  + Argument ("mssh", "the input MSSH prediction.").type_image_in()

  + Argument ("out", "the output motion parameters.").type_file_out();

  OPTIONS
  + Option ("mask", "image mask")
    + Argument ("m").type_image_in()

  + Option ("mb", "multiband factor. (default = 0; v2v registration)")
    + Argument ("factor").type_integer(0)

  + Option ("ssp", "SSP vector or slice thickness in voxel units (default = 1).")
    + Argument ("w").type_text()

  + Option ("init", "motion initialisation")
    + Argument ("motion").type_file_in()

  + Option ("maxiter", "maximum no. iterations for the registration")
    + Argument ("n").type_integer(0)

  + Option ("multiecho", "2nd slice readout in multiecho acquisitions")
    + Argument ("data").type_image_in()
    + Argument ("mssh").type_image_in()

  + DWI::GradImportOptions();

}


using value_type = float;


void run ()
{
  // input data
  auto data = Image<value_type>::open(argument[0]);
  auto grad = DWI::get_DW_scheme (data);

  // input template
  auto mssh = Image<value_type>::open(argument[1]);
  if (mssh.ndim() != 5)
    throw Exception("5-D MSSH image expected.");

  // index shells
  auto bvals = parse_floats(mssh.keyval().find("shells")->second);

  // mask
  auto mask = Image<bool>();
  auto opt = get_options("mask");
  if (opt.size()) {
    mask = Image<bool>::open(opt[0][0]);
    check_dimensions(data, mask, 0, 3);
  }

  // multiband factor
  size_t mb = get_option_value("mb", 0);
  if (mb == 0 || mb == data.size(2)) {
    mb = data.size(2);
    INFO("volume-to-volume registration.");
  } else {
    if (data.size(2) % mb != 0)
      throw Exception ("multiband factor invalid.");
  }

  // SSP
  DWI::SVR::SSP<float> ssp (DEFAULT_SSPW);
  opt = get_options("ssp");
  if (opt.size()) {
    std::string t = opt[0][0];
    try {
      ssp = DWI::SVR::SSP<float>(std::stof(t));
    } catch (std::invalid_argument& e) {
      try {
        Eigen::VectorXf v = load_vector<float>(t);
        ssp = DWI::SVR::SSP<float>(v);
      } catch (...) {
        throw Exception ("Invalid argument for SSP.");
      }
    }
  }

  // settings and initialisation
  size_t niter = get_option_value("maxiter", 0);
  Eigen::MatrixXf init (data.size(3), 6); init.setZero();
  opt = get_options("init");
  if (opt.size()) {
    init = load_matrix<float>(opt[0][0]);
    if (init.cols() != 6 || ((data.size(3)*data.size(2)) % init.rows()))
      throw Exception("dimension mismatch in motion initialisaton.");
  }

  // set up registration
  DWI::SVR::SliceAlignSource source (data.size(3), data.size(2), mb, grad, bvals, init);
  DWI::SVR::SliceAlignPipe pipe (data, mssh, mask, mb, niter, ssp);
  DWI::SVR::SliceAlignSink sink (data.size(3), data.size(2), mb);

  // 2nd echo
  opt = get_options("multiecho");
  if (opt.size()) {
    auto data2 = Image<value_type>::open(opt[0][0]);
    check_dimensions(data, data2);
    auto mssh2 = Image<value_type>::open(opt[0][1]);
    check_dimensions(mssh, mssh2);
    pipe.set_multiecho(data2, mssh2);
  }

  // run registration
  Thread::run_queue(source, DWI::SVR::SliceIdx(), Thread::multi(pipe), DWI::SVR::SliceIdx(), sink);

  // output
  save_matrix(sink.get_motion(), argument[2]);

}



