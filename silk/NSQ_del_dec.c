/***********************************************************************
Copyright (c) 2006-2011, Skype Limited. All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, (subject to the limitations in the disclaimer below)
are permitted provided that the following conditions are met:
- Redistributions of source code must retain the above copyright notice,
this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
- Neither the name of Skype Limited, nor the names of specific
contributors, may be used to endorse or promote products derived from
this software without specific prior written permission.
NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE GRANTED
BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING,
BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "main.h"

typedef struct {
    opus_int32 sLPC_Q14[ MAX_SUB_FRAME_LENGTH + NSQ_LPC_BUF_LENGTH ];
    opus_int32 RandState[ DECISION_DELAY ];
    opus_int32 Q_Q10[     DECISION_DELAY ];
    opus_int32 Xq_Q10[    DECISION_DELAY ];
    opus_int32 Pred_Q16[  DECISION_DELAY ];
    opus_int32 Shape_Q10[ DECISION_DELAY ];
    opus_int32 sAR2_Q14[ MAX_SHAPE_LPC_ORDER ];
    opus_int32 LF_AR_Q12;
    opus_int32 Seed;
    opus_int32 SeedInit;
    opus_int32 RD_Q10;
} NSQ_del_dec_struct;

typedef struct {
    opus_int32 Q_Q10;
    opus_int32 RD_Q10;
    opus_int32 xq_Q14;
    opus_int32 LF_AR_Q12;
    opus_int32 sLTP_shp_Q10;
    opus_int32 LPC_exc_Q16;
} NSQ_sample_struct;

static inline void silk_nsq_del_dec_scale_states(
    const silk_encoder_state *psEncC,               /* I    Encoder State                       */
    silk_nsq_state      *NSQ,                       /* I/O  NSQ state                           */
    NSQ_del_dec_struct  psDelDec[],                 /* I/O  Delayed decision states             */
    const opus_int32    x_Q3[],                     /* I    Input in Q3                         */
    opus_int32          x_sc_Q10[],                 /* O    Input scaled with 1/Gain in Q10     */
    const opus_int16    sLTP[],                     /* I    Re-whitened LTP state in Q0         */
    opus_int32          sLTP_Q15[],                 /* O    LTP state matching scaled input     */
    opus_int            subfr,                      /* I    Subframe number                     */
    opus_int            nStatesDelayedDecision,     /* I    Number of del dec states            */
    const opus_int      LTP_scale_Q14,              /* I    LTP state scaling                   */
    const opus_int32    Gains_Q16[ MAX_NB_SUBFR ],  /* I                                        */
    const opus_int      pitchL[ MAX_NB_SUBFR ],     /* I    Pitch lag                           */
    const opus_int      signal_type,                /* I    Signal type                         */
    const opus_int      decisionDelay               /* I    Decision delay                      */
);

/******************************************/
/* Noise shape quantizer for one subframe */
/******************************************/
static inline void silk_noise_shape_quantizer_del_dec(
    silk_nsq_state      *NSQ,                   /* I/O  NSQ state                           */
    NSQ_del_dec_struct  psDelDec[],             /* I/O  Delayed decision states             */
    opus_int            signalType,             /* I    Signal type                         */
    const opus_int32    x_Q10[],                /* I                                        */
    opus_int8           pulses[],               /* O                                        */
    opus_int16          xq[],                   /* O                                        */
    opus_int32          sLTP_Q15[],             /* I/O  LTP filter state                    */
    opus_int32          delayedGain_Q16[],      /* I/O  Gain delay buffer                   */
    const opus_int16    a_Q12[],                /* I    Short term prediction coefs         */
    const opus_int16    b_Q14[],                /* I    Long term prediction coefs          */
    const opus_int16    AR_shp_Q13[],           /* I    Noise shaping coefs                 */
    opus_int            lag,                    /* I    Pitch lag                           */
    opus_int32          HarmShapeFIRPacked_Q14, /* I                                        */
    opus_int            Tilt_Q14,               /* I    Spectral tilt                       */
    opus_int32          LF_shp_Q14,             /* I                                        */
    opus_int32          Gain_Q16,               /* I                                        */
    opus_int            Lambda_Q10,             /* I                                        */
    opus_int            offset_Q10,             /* I                                        */
    opus_int            length,                 /* I    Input length                        */
    opus_int            subfr,                  /* I    Subframe number                     */
    opus_int            shapingLPCOrder,        /* I    Shaping LPC filter order            */
    opus_int            predictLPCOrder,        /* I    Prediction filter order             */
    opus_int            warping_Q16,            /* I                                        */
    opus_int            nStatesDelayedDecision, /* I    Number of states in decision tree   */
    opus_int            *smpl_buf_idx,          /* I    Index to newest samples in buffers  */
    opus_int            decisionDelay           /* I                                        */
);

void silk_NSQ_del_dec(
    const silk_encoder_state    *psEncC,                                    /* I/O  Encoder State                   */
    silk_nsq_state              *NSQ,                                       /* I/O  NSQ state                       */
    SideInfoIndices             *psIndices,                                 /* I/O  Quantization Indices            */
    const opus_int32            x_Q3[],                                     /* I    Prefiltered input signal        */
    opus_int8                   pulses[],                                   /* O    Quantized pulse signal          */
    const opus_int16            PredCoef_Q12[ 2 * MAX_LPC_ORDER ],          /* I    Short term prediction coefs     */
    const opus_int16            LTPCoef_Q14[ LTP_ORDER * MAX_NB_SUBFR ],    /* I    Long term prediction coefs      */
    const opus_int16            AR2_Q13[ MAX_NB_SUBFR * MAX_SHAPE_LPC_ORDER ], /* I Noise shaping coefs             */
    const opus_int              HarmShapeGain_Q14[ MAX_NB_SUBFR ],          /* I    Long term shaping coefs         */
    const opus_int              Tilt_Q14[ MAX_NB_SUBFR ],                   /* I    Spectral tilt                   */
    const opus_int32            LF_shp_Q14[ MAX_NB_SUBFR ],                 /* I    Low frequency shaping coefs     */
    const opus_int32            Gains_Q16[ MAX_NB_SUBFR ],                  /* I    Quantization step sizes         */
    const opus_int              pitchL[ MAX_NB_SUBFR ],                     /* I    Pitch lags                      */
    const opus_int              Lambda_Q10,                                 /* I    Rate/distortion tradeoff        */
    const opus_int              LTP_scale_Q14                               /* I    LTP state scaling               */
)
{
    opus_int            i, k, lag, start_idx, LSF_interpolation_flag, Winner_ind, subfr;
    opus_int            last_smple_idx, smpl_buf_idx, decisionDelay;
    const opus_int16 	*A_Q12, *B_Q14, *AR_shp_Q13;
    opus_int16          *pxq;
    opus_int32          sLTP_Q15[ 2 * MAX_FRAME_LENGTH ];
    opus_int16          sLTP[     2 * MAX_FRAME_LENGTH ];
    opus_int32          HarmShapeFIRPacked_Q14;
    opus_int            offset_Q10;
    opus_int32          RDmin_Q10;
    opus_int32          x_sc_Q10[ MAX_SUB_FRAME_LENGTH ];
    opus_int32          delayedGain_Q16[  DECISION_DELAY ];
    NSQ_del_dec_struct  psDelDec[ MAX_DEL_DEC_STATES ];
    NSQ_del_dec_struct  *psDD;

    /* Set unvoiced lag to the previous one, overwrite later for voiced */
    lag = NSQ->lagPrev;

    silk_assert( NSQ->prev_inv_gain_Q31 != 0 );

    /* Initialize delayed decision states */
    silk_memset( psDelDec, 0, psEncC->nStatesDelayedDecision * sizeof( NSQ_del_dec_struct ) );
    for( k = 0; k < psEncC->nStatesDelayedDecision; k++ ) {
        psDD                 = &psDelDec[ k ];
        psDD->Seed           = ( k + psIndices->Seed ) & 3;
        psDD->SeedInit       = psDD->Seed;
        psDD->RD_Q10         = 0;
        psDD->LF_AR_Q12      = NSQ->sLF_AR_shp_Q12;
        psDD->Shape_Q10[ 0 ] = NSQ->sLTP_shp_Q10[ psEncC->ltp_mem_length - 1 ];
        silk_memcpy( psDD->sLPC_Q14, NSQ->sLPC_Q14, NSQ_LPC_BUF_LENGTH * sizeof( opus_int32 ) );
        silk_memcpy( psDD->sAR2_Q14, NSQ->sAR2_Q14, sizeof( NSQ->sAR2_Q14 ) );
    }

    offset_Q10   = silk_Quantization_Offsets_Q10[ psIndices->signalType >> 1 ][ psIndices->quantOffsetType ];
    smpl_buf_idx = 0; /* index of oldest samples */

    decisionDelay = silk_min_int( DECISION_DELAY, psEncC->subfr_length );

    /* For voiced frames limit the decision delay to lower than the pitch lag */
    if( psIndices->signalType == TYPE_VOICED ) {
        for( k = 0; k < psEncC->nb_subfr; k++ ) {
            decisionDelay = silk_min_int( decisionDelay, pitchL[ k ] - LTP_ORDER / 2 - 1 );
        }
    } else {
        if( lag > 0 ) {
            decisionDelay = silk_min_int( decisionDelay, lag - LTP_ORDER / 2 - 1 );
        }
    }

    if( psIndices->NLSFInterpCoef_Q2 == 4 ) {
        LSF_interpolation_flag = 0;
    } else {
        LSF_interpolation_flag = 1;
    }

    /* Set up pointers to start of sub frame */
    pxq                   = &NSQ->xq[ psEncC->ltp_mem_length ];
    NSQ->sLTP_shp_buf_idx = psEncC->ltp_mem_length;
    NSQ->sLTP_buf_idx     = psEncC->ltp_mem_length;
    subfr = 0;
    for( k = 0; k < psEncC->nb_subfr; k++ ) {
        A_Q12      = &PredCoef_Q12[ ( ( k >> 1 ) | ( 1 - LSF_interpolation_flag ) ) * MAX_LPC_ORDER ];
        B_Q14      = &LTPCoef_Q14[ k * LTP_ORDER           ];
        AR_shp_Q13 = &AR2_Q13[     k * MAX_SHAPE_LPC_ORDER ];

        /* Noise shape parameters */
        silk_assert( HarmShapeGain_Q14[ k ] >= 0 );
        HarmShapeFIRPacked_Q14  =                          silk_RSHIFT( HarmShapeGain_Q14[ k ], 2 );
        HarmShapeFIRPacked_Q14 |= silk_LSHIFT( (opus_int32)silk_RSHIFT( HarmShapeGain_Q14[ k ], 1 ), 16 );

        NSQ->rewhite_flag = 0;
        if( psIndices->signalType == TYPE_VOICED ) {
            /* Voiced */
            lag = pitchL[ k ];

            /* Re-whitening */
            if( ( k & ( 3 - silk_LSHIFT( LSF_interpolation_flag, 1 ) ) ) == 0 ) {
                if( k == 2 ) {
                    /* RESET DELAYED DECISIONS */
                    /* Find winner */
                    RDmin_Q10 = psDelDec[ 0 ].RD_Q10;
                    Winner_ind = 0;
                    for( i = 1; i < psEncC->nStatesDelayedDecision; i++ ) {
                        if( psDelDec[ i ].RD_Q10 < RDmin_Q10 ) {
                            RDmin_Q10 = psDelDec[ i ].RD_Q10;
                            Winner_ind = i;
                        }
                    }
                    for( i = 0; i < psEncC->nStatesDelayedDecision; i++ ) {
                        if( i != Winner_ind ) {
                            psDelDec[ i ].RD_Q10 += ( silk_int32_MAX >> 4 );
                            silk_assert( psDelDec[ i ].RD_Q10 >= 0 );
                        }
                    }

                    /* Copy final part of signals from winner state to output and long-term filter states */
                    psDD = &psDelDec[ Winner_ind ];
                    last_smple_idx = smpl_buf_idx + decisionDelay;
                    for( i = 0; i < decisionDelay; i++ ) {
                        last_smple_idx = ( last_smple_idx - 1 ) & DECISION_DELAY_MASK;
                        pulses[   i - decisionDelay ] = (opus_int8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
                        pxq[ i - decisionDelay ] = (opus_int16)silk_SAT16( silk_RSHIFT_ROUND(
                            silk_SMULWW( psDD->Xq_Q10[ last_smple_idx ], Gains_Q16[ 1 ] ), 10 ) );
                        NSQ->sLTP_shp_Q10[ NSQ->sLTP_shp_buf_idx - decisionDelay + i ] = psDD->Shape_Q10[ last_smple_idx ];
                    }

                    subfr = 0;
                }

                /* Rewhiten with new A coefs */
                start_idx = psEncC->ltp_mem_length - lag - psEncC->predictLPCOrder - LTP_ORDER / 2;
                silk_assert( start_idx > 0 );

                silk_LPC_analysis_filter( &sLTP[ start_idx ], &NSQ->xq[ start_idx + k * psEncC->subfr_length ],
                    A_Q12, psEncC->ltp_mem_length - start_idx, psEncC->predictLPCOrder );

                NSQ->sLTP_buf_idx = psEncC->ltp_mem_length;
                NSQ->rewhite_flag = 1;
            }
        }

        silk_nsq_del_dec_scale_states( psEncC, NSQ, psDelDec, x_Q3, x_sc_Q10, sLTP, sLTP_Q15, k,
            psEncC->nStatesDelayedDecision, LTP_scale_Q14, Gains_Q16, pitchL, psIndices->signalType, decisionDelay );

        silk_noise_shape_quantizer_del_dec( NSQ, psDelDec, psIndices->signalType, x_sc_Q10, pulses, pxq, sLTP_Q15,
            delayedGain_Q16, A_Q12, B_Q14, AR_shp_Q13, lag, HarmShapeFIRPacked_Q14, Tilt_Q14[ k ], LF_shp_Q14[ k ],
            Gains_Q16[ k ], Lambda_Q10, offset_Q10, psEncC->subfr_length, subfr++, psEncC->shapingLPCOrder,
            psEncC->predictLPCOrder, psEncC->warping_Q16, psEncC->nStatesDelayedDecision, &smpl_buf_idx, decisionDelay );

        x_Q3   += psEncC->subfr_length;
        pulses += psEncC->subfr_length;
        pxq    += psEncC->subfr_length;
    }

    /* Find winner */
    RDmin_Q10 = psDelDec[ 0 ].RD_Q10;
    Winner_ind = 0;
    for( k = 1; k < psEncC->nStatesDelayedDecision; k++ ) {
        if( psDelDec[ k ].RD_Q10 < RDmin_Q10 ) {
            RDmin_Q10 = psDelDec[ k ].RD_Q10;
            Winner_ind = k;
        }
    }

    /* Copy final part of signals from winner state to output and long-term filter states */
    psDD = &psDelDec[ Winner_ind ];
    psIndices->Seed = psDD->SeedInit;
    last_smple_idx = smpl_buf_idx + decisionDelay;
    for( i = 0; i < decisionDelay; i++ ) {
        last_smple_idx = ( last_smple_idx - 1 ) & DECISION_DELAY_MASK;
        pulses[   i - decisionDelay ] = (opus_int8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
        pxq[ i - decisionDelay ] = (opus_int16)silk_SAT16( silk_RSHIFT_ROUND(
            silk_SMULWW( psDD->Xq_Q10[ last_smple_idx ], Gains_Q16[ psEncC->nb_subfr - 1 ] ), 10 ) );
        NSQ->sLTP_shp_Q10[ NSQ->sLTP_shp_buf_idx - decisionDelay + i ] = psDD->Shape_Q10[ last_smple_idx ];
    }
    silk_memcpy( NSQ->sLPC_Q14, &psDD->sLPC_Q14[ psEncC->subfr_length ], NSQ_LPC_BUF_LENGTH * sizeof( opus_int32 ) );
    silk_memcpy( NSQ->sAR2_Q14, psDD->sAR2_Q14, sizeof( psDD->sAR2_Q14 ) );

    /* Update states */
    NSQ->sLF_AR_shp_Q12 = psDD->LF_AR_Q12;
    NSQ->lagPrev        = pitchL[ psEncC->nb_subfr - 1 ];

    /* Save quantized speech signal */
    /* DEBUG_STORE_DATA( enc.pcm, &NSQ->xq[psEncC->ltp_mem_length], psEncC->frame_length * sizeof( opus_int16 ) ) */
    silk_memmove( NSQ->xq,           &NSQ->xq[           psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( opus_int16 ) );
    silk_memmove( NSQ->sLTP_shp_Q10, &NSQ->sLTP_shp_Q10[ psEncC->frame_length ], psEncC->ltp_mem_length * sizeof( opus_int32 ) );
}

/******************************************/
/* Noise shape quantizer for one subframe */
/******************************************/
static inline void silk_noise_shape_quantizer_del_dec(
    silk_nsq_state      *NSQ,                   /* I/O  NSQ state                           */
    NSQ_del_dec_struct  psDelDec[],             /* I/O  Delayed decision states             */
    opus_int            signalType,             /* I    Signal type                         */
    const opus_int32    x_Q10[],                /* I                                        */
    opus_int8           pulses[],               /* O                                        */
    opus_int16          xq[],                   /* O                                        */
    opus_int32          sLTP_Q15[],             /* I/O  LTP filter state                    */
    opus_int32          delayedGain_Q16[],      /* I/O  Gain delay buffer                   */
    const opus_int16    a_Q12[],                /* I    Short term prediction coefs         */
    const opus_int16    b_Q14[],                /* I    Long term prediction coefs          */
    const opus_int16    AR_shp_Q13[],           /* I    Noise shaping coefs                 */
    opus_int            lag,                    /* I    Pitch lag                           */
    opus_int32          HarmShapeFIRPacked_Q14, /* I                                        */
    opus_int            Tilt_Q14,               /* I    Spectral tilt                       */
    opus_int32          LF_shp_Q14,             /* I                                        */
    opus_int32          Gain_Q16,               /* I                                        */
    opus_int            Lambda_Q10,             /* I                                        */
    opus_int            offset_Q10,             /* I                                        */
    opus_int            length,                 /* I    Input length                        */
    opus_int            subfr,                  /* I    Subframe number                     */
    opus_int            shapingLPCOrder,        /* I    Shaping LPC filter order            */
    opus_int            predictLPCOrder,        /* I    Prediction filter order             */
    opus_int            warping_Q16,            /* I                                        */
    opus_int            nStatesDelayedDecision, /* I    Number of states in decision tree   */
    opus_int            *smpl_buf_idx,          /* I    Index to newest samples in buffers  */
    opus_int            decisionDelay           /* I                                        */
)
{
    opus_int     i, j, k, Winner_ind, RDmin_ind, RDmax_ind, last_smple_idx;
    opus_int32   Winner_rand_state;
    opus_int32   LTP_pred_Q13, LPC_pred_Q10, n_AR_Q10, n_LTP_Q14, LTP_Q10;
    opus_int32   n_LF_Q10, r_Q10, rr_Q10, rd1_Q10, rd2_Q10, RDmin_Q10, RDmax_Q10;
    opus_int32   q1_Q10, q2_Q10, dither, exc_Q10, LPC_exc_Q10, xq_Q10;
    opus_int32   tmp1, tmp2, sLF_AR_shp_Q10;
    opus_int32   *pred_lag_ptr, *shp_lag_ptr, *psLPC_Q14;
    NSQ_sample_struct  psSampleState[ MAX_DEL_DEC_STATES ][ 2 ];
    NSQ_del_dec_struct *psDD;
    NSQ_sample_struct  *psSS;

    silk_assert( nStatesDelayedDecision > 0 );

    shp_lag_ptr  = &NSQ->sLTP_shp_Q10[ NSQ->sLTP_shp_buf_idx - lag + HARM_SHAPE_FIR_TAPS / 2 ];
    pred_lag_ptr = &sLTP_Q15[ NSQ->sLTP_buf_idx - lag + LTP_ORDER / 2 ];

    for( i = 0; i < length; i++ ) {
        /* Perform common calculations used in all states */

        /* Long-term prediction */
        if( signalType == TYPE_VOICED ) {
            /* Unrolled loop */
            /* Avoids introducing a bias because silk_SMLAWB() always rounds to -inf */
            LTP_pred_Q13 = 2;
            LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[  0 ], b_Q14[ 0 ] );
            LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -1 ], b_Q14[ 1 ] );
            LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -2 ], b_Q14[ 2 ] );
            LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -3 ], b_Q14[ 3 ] );
            LTP_pred_Q13 = silk_SMLAWB( LTP_pred_Q13, pred_lag_ptr[ -4 ], b_Q14[ 4 ] );
            pred_lag_ptr++;
        } else {
            LTP_pred_Q13 = 0;
        }

        /* Long-term shaping */
        if( lag > 0 ) {
            /* Symmetric, packed FIR coefficients */
            n_LTP_Q14 = silk_SMULWB( silk_ADD32( shp_lag_ptr[ 0 ], shp_lag_ptr[ -2 ] ), HarmShapeFIRPacked_Q14 );
            n_LTP_Q14 = silk_SMLAWT( n_LTP_Q14, shp_lag_ptr[ -1 ],                      HarmShapeFIRPacked_Q14 );
            n_LTP_Q14 = silk_LSHIFT( n_LTP_Q14, 6 );
            shp_lag_ptr++;

            LTP_Q10 = silk_RSHIFT( silk_SUB32( silk_LSHIFT( LTP_pred_Q13, 1 ), n_LTP_Q14 ), 4 );
        } else {
            LTP_Q10 = 0;
        }

        for( k = 0; k < nStatesDelayedDecision; k++ ) {
            /* Delayed decision state */
            psDD = &psDelDec[ k ];

            /* Sample state */
            psSS = psSampleState[ k ];

            /* Generate dither */
            psDD->Seed = silk_RAND( psDD->Seed );

            /* dither = rand_seed < 0 ? 0xFFFFFFFF : 0; */
            dither = silk_RSHIFT( psDD->Seed, 31 );

            /* Pointer used in short term prediction and shaping */
            psLPC_Q14 = &psDD->sLPC_Q14[ NSQ_LPC_BUF_LENGTH - 1 + i ];
            /* Short-term prediction */
            silk_assert( predictLPCOrder == 10 || predictLPCOrder == 16 );
            /* Avoids introducing a bias because silk_SMLAWB() always rounds to -inf */
            LPC_pred_Q10 = silk_RSHIFT( predictLPCOrder, 1 );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[  0 ], a_Q12[ 0 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -1 ], a_Q12[ 1 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -2 ], a_Q12[ 2 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -3 ], a_Q12[ 3 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -4 ], a_Q12[ 4 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -5 ], a_Q12[ 5 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -6 ], a_Q12[ 6 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -7 ], a_Q12[ 7 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -8 ], a_Q12[ 8 ] );
            LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -9 ], a_Q12[ 9 ] );
            if( predictLPCOrder == 16 ) {
                LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -10 ], a_Q12[ 10 ] );
                LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -11 ], a_Q12[ 11 ] );
                LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -12 ], a_Q12[ 12 ] );
                LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -13 ], a_Q12[ 13 ] );
                LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -14 ], a_Q12[ 14 ] );
                LPC_pred_Q10 = silk_SMLAWB( LPC_pred_Q10, psLPC_Q14[ -15 ], a_Q12[ 15 ] );
            }

            /* Noise shape feedback */
            silk_assert( ( shapingLPCOrder & 1 ) == 0 );   /* check that order is even */
            /* Output of lowpass section */
            tmp2 = silk_SMLAWB( psLPC_Q14[ 0 ], psDD->sAR2_Q14[ 0 ], warping_Q16 );
            /* Output of allpass section */
            tmp1 = silk_SMLAWB( psDD->sAR2_Q14[ 0 ], psDD->sAR2_Q14[ 1 ] - tmp2, warping_Q16 );
            psDD->sAR2_Q14[ 0 ] = tmp2;
            n_AR_Q10 = silk_SMULWB( tmp2, AR_shp_Q13[ 0 ] );
            /* Loop over allpass sections */
            for( j = 2; j < shapingLPCOrder; j += 2 ) {
                /* Output of allpass section */
                tmp2 = silk_SMLAWB( psDD->sAR2_Q14[ j - 1 ], psDD->sAR2_Q14[ j + 0 ] - tmp1, warping_Q16 );
                psDD->sAR2_Q14[ j - 1 ] = tmp1;
                n_AR_Q10 = silk_SMLAWB( n_AR_Q10, tmp1, AR_shp_Q13[ j - 1 ] );
                /* Output of allpass section */
                tmp1 = silk_SMLAWB( psDD->sAR2_Q14[ j + 0 ], psDD->sAR2_Q14[ j + 1 ] - tmp2, warping_Q16 );
                psDD->sAR2_Q14[ j + 0 ] = tmp2;
                n_AR_Q10 = silk_SMLAWB( n_AR_Q10, tmp2, AR_shp_Q13[ j ] );
            }
            psDD->sAR2_Q14[ shapingLPCOrder - 1 ] = tmp1;
            n_AR_Q10 = silk_SMLAWB( n_AR_Q10, tmp1, AR_shp_Q13[ shapingLPCOrder - 1 ] );

            n_AR_Q10 = silk_RSHIFT( n_AR_Q10, 1 );           /* Q11 -> Q10 */
            n_AR_Q10 = silk_SMLAWB( n_AR_Q10, psDD->LF_AR_Q12, Tilt_Q14 );

            n_LF_Q10 = silk_LSHIFT( silk_SMULWB( psDD->Shape_Q10[ *smpl_buf_idx ], LF_shp_Q14 ), 2 );
            n_LF_Q10 = silk_SMLAWT( n_LF_Q10, psDD->LF_AR_Q12, LF_shp_Q14 );

            /* Input minus prediction plus noise feedback                       */
            /* r = x[ i ] - LTP_pred - LPC_pred + n_AR + n_Tilt + n_LF + n_LTP  */
            tmp1  = silk_ADD32( LTP_Q10, LPC_pred_Q10 );                         /* add Q10 stuff */
            tmp1  = silk_SUB32( tmp1, n_AR_Q10 );                                /* subtract Q10 stuff */
            tmp1  = silk_SUB32( tmp1, n_LF_Q10 );                                /* subtract Q10 stuff */
            r_Q10 = silk_SUB32( x_Q10[ i ], tmp1 );                              /* residual error Q10 */

            /* Flip sign depending on dither */
            r_Q10 = r_Q10 ^ dither;
            r_Q10 = silk_LIMIT_32( r_Q10, -(31 << 10), 30 << 10 );

            /* Find two quantization level candidates and measure their rate-distortion */
            q1_Q10 = silk_SUB32( r_Q10, offset_Q10 );
            q1_Q10 = silk_RSHIFT( q1_Q10, 10 );
            if( q1_Q10 > 0 ) {
                q1_Q10  = silk_SUB32( silk_LSHIFT( q1_Q10, 10 ), QUANT_LEVEL_ADJUST_Q10 );
                q1_Q10  = silk_ADD32( q1_Q10, offset_Q10 );
                q2_Q10  = silk_ADD32( q1_Q10, 1024 );
                rd1_Q10 = silk_SMULBB( q1_Q10, Lambda_Q10 );
                rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
            } else if( q1_Q10 == 0 ) {
                q1_Q10  = offset_Q10;
                q2_Q10  = silk_ADD32( q1_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
                rd1_Q10 = silk_SMULBB( q1_Q10, Lambda_Q10 );
                rd2_Q10 = silk_SMULBB( q2_Q10, Lambda_Q10 );
            } else if( q1_Q10 == -1 ) {
                q2_Q10  = offset_Q10;
                q1_Q10  = silk_SUB32( q2_Q10, 1024 - QUANT_LEVEL_ADJUST_Q10 );
                rd1_Q10 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
                rd2_Q10 = silk_SMULBB(  q2_Q10, Lambda_Q10 );
            } else {            /* Q1_Q10 < -1 */
                q1_Q10  = silk_ADD32( silk_LSHIFT( q1_Q10, 10 ), QUANT_LEVEL_ADJUST_Q10 );
                q1_Q10  = silk_ADD32( q1_Q10, offset_Q10 );
                q2_Q10  = silk_ADD32( q1_Q10, 1024 );
                rd1_Q10 = silk_SMULBB( -q1_Q10, Lambda_Q10 );
                rd2_Q10 = silk_SMULBB( -q2_Q10, Lambda_Q10 );
            }
            rr_Q10  = silk_SUB32( r_Q10, q1_Q10 );
            rd1_Q10 = silk_RSHIFT( silk_SMLABB( rd1_Q10, rr_Q10, rr_Q10 ), 10 );
            rr_Q10  = silk_SUB32( r_Q10, q2_Q10 );
            rd2_Q10 = silk_RSHIFT( silk_SMLABB( rd2_Q10, rr_Q10, rr_Q10 ), 10 );

            if( rd1_Q10 < rd2_Q10 ) {
                psSS[ 0 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd1_Q10 );
                psSS[ 1 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd2_Q10 );
                psSS[ 0 ].Q_Q10  = q1_Q10;
                psSS[ 1 ].Q_Q10  = q2_Q10;
            } else {
                psSS[ 0 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd2_Q10 );
                psSS[ 1 ].RD_Q10 = silk_ADD32( psDD->RD_Q10, rd1_Q10 );
                psSS[ 0 ].Q_Q10  = q2_Q10;
                psSS[ 1 ].Q_Q10  = q1_Q10;
            }

            /* Update states for best quantization */

            /* Quantized excitation */
            exc_Q10 = psSS[ 0 ].Q_Q10 ^ dither;

            /* Add predictions */
            LPC_exc_Q10 = exc_Q10 + silk_RSHIFT_ROUND( LTP_pred_Q13, 3 );
            xq_Q10      = silk_ADD32( LPC_exc_Q10, LPC_pred_Q10 );

            /* Update states */
            sLF_AR_shp_Q10         = silk_SUB32(  xq_Q10, n_AR_Q10 );
            psSS[ 0 ].sLTP_shp_Q10 = silk_SUB32(  sLF_AR_shp_Q10, n_LF_Q10 );
            psSS[ 0 ].LF_AR_Q12    = silk_LSHIFT( sLF_AR_shp_Q10, 2 );
            psSS[ 0 ].xq_Q14       = silk_LSHIFT( xq_Q10,         4 );
            psSS[ 0 ].LPC_exc_Q16  = silk_LSHIFT( LPC_exc_Q10,    6 );

            /* Update states for second best quantization */

            /* Quantized excitation */
            exc_Q10 = psSS[ 1 ].Q_Q10 ^ dither;

            /* Add predictions */
            LPC_exc_Q10 = exc_Q10 + silk_RSHIFT_ROUND( LTP_pred_Q13, 3 );
            xq_Q10      = silk_ADD32( LPC_exc_Q10, LPC_pred_Q10 );

            /* Update states */
            sLF_AR_shp_Q10         = silk_SUB32(  xq_Q10, n_AR_Q10 );
            psSS[ 1 ].sLTP_shp_Q10 = silk_SUB32(  sLF_AR_shp_Q10, n_LF_Q10 );
            psSS[ 1 ].LF_AR_Q12    = silk_LSHIFT( sLF_AR_shp_Q10, 2 );
            psSS[ 1 ].xq_Q14       = silk_LSHIFT( xq_Q10,         4 );
            psSS[ 1 ].LPC_exc_Q16  = silk_LSHIFT( LPC_exc_Q10,    6 );
        }

        *smpl_buf_idx  = ( *smpl_buf_idx - 1 ) & DECISION_DELAY_MASK;                   /* Index to newest samples              */
        last_smple_idx = ( *smpl_buf_idx + decisionDelay ) & DECISION_DELAY_MASK;       /* Index to decisionDelay old samples   */

        /* Find winner */
        RDmin_Q10 = psSampleState[ 0 ][ 0 ].RD_Q10;
        Winner_ind = 0;
        for( k = 1; k < nStatesDelayedDecision; k++ ) {
            if( psSampleState[ k ][ 0 ].RD_Q10 < RDmin_Q10 ) {
                RDmin_Q10  = psSampleState[ k ][ 0 ].RD_Q10;
                Winner_ind = k;
            }
        }

        /* Increase RD values of expired states */
        Winner_rand_state = psDelDec[ Winner_ind ].RandState[ last_smple_idx ];
        for( k = 0; k < nStatesDelayedDecision; k++ ) {
            if( psDelDec[ k ].RandState[ last_smple_idx ] != Winner_rand_state ) {
                psSampleState[ k ][ 0 ].RD_Q10 = silk_ADD32( psSampleState[ k ][ 0 ].RD_Q10, ( silk_int32_MAX >> 4 ) );
                psSampleState[ k ][ 1 ].RD_Q10 = silk_ADD32( psSampleState[ k ][ 1 ].RD_Q10, ( silk_int32_MAX >> 4 ) );
                silk_assert( psSampleState[ k ][ 0 ].RD_Q10 >= 0 );
            }
        }

        /* Find worst in first set and best in second set */
        RDmax_Q10  = psSampleState[ 0 ][ 0 ].RD_Q10;
        RDmin_Q10  = psSampleState[ 0 ][ 1 ].RD_Q10;
        RDmax_ind = 0;
        RDmin_ind = 0;
        for( k = 1; k < nStatesDelayedDecision; k++ ) {
            /* find worst in first set */
            if( psSampleState[ k ][ 0 ].RD_Q10 > RDmax_Q10 ) {
                RDmax_Q10  = psSampleState[ k ][ 0 ].RD_Q10;
                RDmax_ind = k;
            }
            /* find best in second set */
            if( psSampleState[ k ][ 1 ].RD_Q10 < RDmin_Q10 ) {
                RDmin_Q10  = psSampleState[ k ][ 1 ].RD_Q10;
                RDmin_ind = k;
            }
        }

        /* Replace a state if best from second set outperforms worst in first set */
        if( RDmin_Q10 < RDmax_Q10 ) {
            silk_memcpy( ((opus_int32 *)&psDelDec[ RDmax_ind ]) + i,
                        ((opus_int32 *)&psDelDec[ RDmin_ind ]) + i, sizeof( NSQ_del_dec_struct ) - i * sizeof( opus_int32) );
            silk_memcpy( &psSampleState[ RDmax_ind ][ 0 ], &psSampleState[ RDmin_ind ][ 1 ], sizeof( NSQ_sample_struct ) );
        }

        /* Write samples from winner to output and long-term filter states */
        psDD = &psDelDec[ Winner_ind ];
        if( subfr > 0 || i >= decisionDelay ) {
            pulses[  i - decisionDelay ] = (opus_int8)silk_RSHIFT_ROUND( psDD->Q_Q10[ last_smple_idx ], 10 );
            xq[ i - decisionDelay ] = (opus_int16)silk_SAT16( silk_RSHIFT_ROUND(
                silk_SMULWW( psDD->Xq_Q10[ last_smple_idx ], delayedGain_Q16[ last_smple_idx ] ), 10 ) );
            NSQ->sLTP_shp_Q10[ NSQ->sLTP_shp_buf_idx - decisionDelay ] = psDD->Shape_Q10[ last_smple_idx ];
            sLTP_Q15[          NSQ->sLTP_buf_idx     - decisionDelay ] = psDD->Pred_Q16[  last_smple_idx ] >> 1;
        }
        NSQ->sLTP_shp_buf_idx++;
        NSQ->sLTP_buf_idx++;

        /* Update states */
        for( k = 0; k < nStatesDelayedDecision; k++ ) {
            psDD                                     = &psDelDec[ k ];
            psSS                                     = &psSampleState[ k ][ 0 ];
            psDD->LF_AR_Q12                          = psSS->LF_AR_Q12;
            psDD->sLPC_Q14[ NSQ_LPC_BUF_LENGTH + i ] = psSS->xq_Q14;
            psDD->Xq_Q10[    *smpl_buf_idx ]         = silk_RSHIFT( psSS->xq_Q14, 4 );
            psDD->Q_Q10[     *smpl_buf_idx ]         = psSS->Q_Q10;
            psDD->Pred_Q16[  *smpl_buf_idx ]         = psSS->LPC_exc_Q16;
            psDD->Shape_Q10[ *smpl_buf_idx ]         = psSS->sLTP_shp_Q10;
            psDD->Seed                               = silk_ADD32_ovflw( psDD->Seed, silk_RSHIFT_ROUND( psSS->Q_Q10, 10 ) );
            psDD->RandState[ *smpl_buf_idx ]         = psDD->Seed;
            psDD->RD_Q10                             = psSS->RD_Q10;
        }
        delayedGain_Q16[     *smpl_buf_idx ]         = Gain_Q16;
    }
    /* Update LPC states */
    for( k = 0; k < nStatesDelayedDecision; k++ ) {
        psDD = &psDelDec[ k ];
        silk_memcpy( psDD->sLPC_Q14, &psDD->sLPC_Q14[ length ], NSQ_LPC_BUF_LENGTH * sizeof( opus_int32 ) );
    }
}

static inline void silk_nsq_del_dec_scale_states(
    const silk_encoder_state *psEncC,               /* I    Encoder State                       */
    silk_nsq_state      *NSQ,                       /* I/O  NSQ state                           */
    NSQ_del_dec_struct  psDelDec[],                 /* I/O  Delayed decision states             */
    const opus_int32    x_Q3[],                     /* I    Input in Q3                         */
    opus_int32          x_sc_Q10[],                 /* O    Input scaled with 1/Gain in Q10     */
    const opus_int16    sLTP[],                     /* I    Re-whitened LTP state in Q0         */
    opus_int32          sLTP_Q15[],                 /* O    LTP state matching scaled input     */
    opus_int            subfr,                      /* I    Subframe number                     */
    opus_int            nStatesDelayedDecision,     /* I    Number of del dec states            */
    const opus_int      LTP_scale_Q14,              /* I    LTP state scaling                   */
    const opus_int32    Gains_Q16[ MAX_NB_SUBFR ],  /* I                                        */
    const opus_int      pitchL[ MAX_NB_SUBFR ],     /* I    Pitch lag                           */
    const opus_int      signal_type,                /* I    Signal type                         */
    const opus_int      decisionDelay               /* I    Decision delay                      */
)
{
    opus_int            i, k, lag;
    opus_int32          gain_adj_Q16, inv_gain_Q31, inv_gain_Q23;
    NSQ_del_dec_struct  *psDD;

    inv_gain_Q31 = silk_INVERSE32_varQ( silk_max( Gains_Q16[ subfr ], 1 ), 47 );
    lag          = pitchL[ subfr ];

    /* Calculate gain adjustment factor */
    if( inv_gain_Q31 != NSQ->prev_inv_gain_Q31 ) {
        gain_adj_Q16 =  silk_DIV32_varQ( inv_gain_Q31, NSQ->prev_inv_gain_Q31, 16 );
    } else {
        gain_adj_Q16 = 1 << 16;
    }

    /* Scale input */
    inv_gain_Q23 = silk_RSHIFT_ROUND( inv_gain_Q31, 8 );
    for( i = 0; i < psEncC->subfr_length; i++ ) {
        x_sc_Q10[ i ] = silk_SMULWW( x_Q3[ i ], inv_gain_Q23 );
    }

    /* Save inverse gain */
    silk_assert( inv_gain_Q31 != 0 );
    NSQ->prev_inv_gain_Q31 = inv_gain_Q31;

    /* After rewhitening the LTP state is un-scaled, so scale with inv_gain_Q16 */
    if( NSQ->rewhite_flag ) {
        if( subfr == 0 ) {
            /* Do LTP downscaling */
            inv_gain_Q31 = silk_LSHIFT( silk_SMULWB( inv_gain_Q31, LTP_scale_Q14 ), 2 );
        }
        for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx; i++ ) {
            silk_assert( i < MAX_FRAME_LENGTH );
            sLTP_Q15[ i ] = silk_SMULWB( inv_gain_Q31, sLTP[ i ] );
        }
    }

    /* Adjust for changing gain */
    if( gain_adj_Q16 != 1 << 16 ) {
        /* Scale long-term shaping state */
        for( i = NSQ->sLTP_shp_buf_idx - psEncC->ltp_mem_length; i < NSQ->sLTP_shp_buf_idx; i++ ) {
            NSQ->sLTP_shp_Q10[ i ] = silk_SMULWW( gain_adj_Q16, NSQ->sLTP_shp_Q10[ i ] );
        }

        /* Scale long-term prediction state */
        if( signal_type == TYPE_VOICED && NSQ->rewhite_flag == 0 ) {
            for( i = NSQ->sLTP_buf_idx - lag - LTP_ORDER / 2; i < NSQ->sLTP_buf_idx - decisionDelay; i++ ) {
                sLTP_Q15[ i ] = silk_SMULWW( gain_adj_Q16, sLTP_Q15[ i ] );
            }
        }

        for( k = 0; k < nStatesDelayedDecision; k++ ) {
            psDD = &psDelDec[ k ];

            /* Scale scalar states */
            psDD->LF_AR_Q12 = silk_SMULWW( gain_adj_Q16, psDD->LF_AR_Q12 );

            /* Scale short-term prediction and shaping states */
            for( i = 0; i < NSQ_LPC_BUF_LENGTH; i++ ) {
                psDD->sLPC_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->sLPC_Q14[ i ] );
            }
            for( i = 0; i < MAX_SHAPE_LPC_ORDER; i++ ) {
                psDD->sAR2_Q14[ i ] = silk_SMULWW( gain_adj_Q16, psDD->sAR2_Q14[ i ] );
            }
            for( i = 0; i < DECISION_DELAY; i++ ) {
                psDD->Pred_Q16[  i ] = silk_SMULWW( gain_adj_Q16, psDD->Pred_Q16[  i ] );
                psDD->Shape_Q10[ i ] = silk_SMULWW( gain_adj_Q16, psDD->Shape_Q10[ i ] );
            }
        }
    }
}
